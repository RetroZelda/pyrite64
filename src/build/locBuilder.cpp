/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, sections 4.8 and 5). Compiles every text of every
// language, bakes the fonts of the font sets with exactly the characters the texts use, and writes
// one string table per language (filesystem/p64/loc/<code>.loc), the fonts
// (filesystem/p64/loc/f/<set>_<variant>_<size>.font64) and src/p64/locTable.cpp.
#include "projectBuilder.h"

#include <filesystem>
#include <format>
#include <map>
#include <regex>
#include <set>
#include <tuple>
#include <algorithm>

#include "../project/project.h"
#include "../project/locale/locProject.h"
#include "../utils/fs.h"
#include "../utils/hash.h"
#include "../utils/logger.h"
#include "../../n64/engine/include/text/locId.h"
#include "../../n64/engine/include/text/locTable.h"

namespace fs = std::filesystem;

namespace
{
  using namespace Project::Loc;

  constexpr uint8_t FIRST_FONT_ID = 16;
  const char* const VARIANT_NAMES[4] = {"regular", "bold", "italic", "boldItalic"};

  std::string cString(const std::string &s)
  {
    std::string out = "\"";
    for(unsigned char c : s) {
      if(c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
      else if(c < 0x20) out += std::format("\\x{:02x}\"\"", c);
      else out += static_cast<char>(c);
    }
    return out + "\"";
  }

  std::string cChar(char c) { return std::format("'\\x{:02x}'", static_cast<unsigned char>(c)); }

  bool saveBinaryIfChanged(const fs::path &path, const std::vector<uint8_t> &data)
  {
    std::string bytes(data.begin(), data.end());
    return Utils::FS::saveTextFileIfChanged(path, bytes);
  }

  std::string utf8Of(const std::set<uint32_t> &cps)
  {
    std::string out{};
    for(uint32_t cp : cps) {
      char buf[4];
      const int n = P64::Loc::utf8Encode(cp, buf, 4);
      out.append(buf, static_cast<size_t>(n));
    }
    return out;
  }

  struct BakedFont
  {
    std::string romPath{};
    uint8_t id{};
    uint8_t set{};
    uint8_t variant{};
    bool colored{};
  };

  struct Output
  {
    const Localization *loc{};
    const CompileEnv *env{};
    std::vector<BakedFont> fonts{};
    std::vector<std::array<std::array<uint8_t, 4>, 4>> ids{}; // [set][variant][size]
    uint8_t fallbackColor{0};
  };

  void writeGenerated(Project::Project &project, Build::SceneCtx &ctx, const Output *outp)
  {
    std::string src = "// NOTE: Auto-Generated File!\n// Fork: localization, the project's languages and fonts (locale/locale.json).\n\n"
                      "#include \"text/loc.h\"\n\n";
    if(!outp || !outp->loc || outp->loc->config.languages.empty()) {
      src += "void P64::Loc::Generated::registerLanguages() {}\n";
      Utils::FS::saveTextFileIfChanged(project.getPath() + "/src/p64/locTable.cpp", src);
      return;
    }
    const auto &o = *outp;
    const auto &cfg = o.loc->config;
    const auto &env = *o.env;
    const auto *srcLang = cfg.find(cfg.source);

    src += "namespace\n{\n  constexpr P64::Loc::Language LANGUAGES[] = {\n";
    for(const auto &lang : cfg.languages) {
      const int pair = cfg.indexOf(cfg.pairOf(lang.code));
      uint8_t flags = 0;
      if(lang.pseudo) flags |= 1 << 0;           // LANG_PSEUDO
      if(lang.wrap == "char") flags |= 1 << 1;   // LANG_WRAP_CHAR
      const auto &ruleLang = (lang.pseudo && srcLang) ? *srcLang : lang;
      const auto rule = pluralRuleOf(ruleLang);
      const auto [sep, min] = groupingOf(ruleLang);
      const int set = env.fontSetIndex(lang.fonts);
      src += std::format("    {{{}, {}, {}, {}, {}, {}, {}, {:.3f}f, {}, {}}},\n",
        cString(lang.code), cString(lang.name), cString("rom:/p64/loc/" + lang.code + ".loc"),
        pair < 0 ? std::string{"P64::Loc::NO_LANGUAGE"} : std::to_string(pair), flags,
        static_cast<int>(rule), set < 0 ? 0xFF : set, lang.revealScale, cChar(sep), min);
    }
    src += "  };\n\n";

    // font sets
    if(!env.fontSets.empty()) {
      src += "  constexpr P64::Loc::FontSet SETS[] = {\n";
      for(size_t s = 0; s < env.fontSets.size(); ++s) {
        const auto &fs = env.fontSets[s];
        std::string ids{};
        for(int v = 0; v < 4; ++v) {
          ids += std::format("{{{}, {}, {}, {}}}{}", o.ids[s][v][0], o.ids[s][v][1], o.ids[s][v][2], o.ids[s][v][3], v < 3 ? ", " : "");
        }
        src += std::format("    {{{}, {{{}}}, {}, {}, 0x{:08X}u}},\n", cString(fs.name), ids, fs.sizes.size(), fs.base, fs.outlineColor);
      }
      src += "  };\n";
    }
    if(!o.fonts.empty()) {
      src += "  constexpr P64::Loc::FontFile FONTS[] = {\n";
      for(const auto &f : o.fonts) {
        src += std::format("    {{{}, {}, {}, {}, {}}},\n", cString(f.romPath), f.id, f.set, f.variant, f.colored ? 1 : 0);
      }
      src += "  };\n";
    }
    src += "  constexpr uint32_t PALETTE[] = {";
    for(size_t i = 0; i < env.paletteRgba.size(); ++i) src += std::format("{}0x{:08X}u", i ? ", " : "", env.paletteRgba[i]);
    src += "};\n";
    src += "  const char* const EFFECTS[] = {";
    for(size_t i = 0; i < env.effects.size(); ++i) src += (i ? ", " : "") + cString(env.effects[i]);
    src += "};\n";
    for(size_t c = 0; c < env.controls.size(); ++c) {
      if(env.controls[c].names.empty()) continue;
      src += std::format("  const char* const CONTROL_{}_NAMES[] = {{", c);
      for(size_t i = 0; i < env.controls[c].names.size(); ++i) src += (i ? ", " : "") + cString(env.controls[c].names[i]);
      src += "};\n";
    }
    if(!env.controls.empty()) {
      src += "  constexpr P64::Loc::ControlInfo CONTROLS[] = {\n";
      for(size_t c = 0; c < env.controls.size(); ++c) {
        const auto &ctl = env.controls[c];
        const int kind = ctl.kind == "seconds" ? 1 : ctl.kind == "number" ? 2 : ctl.kind == "name" ? 3 : 0;
        src += std::format("    {{{}, {}, {}, {}}},\n", cString(ctl.name), kind,
          ctl.names.empty() ? std::string{"nullptr"} : std::format("CONTROL_{}_NAMES", c), ctl.names.size());
      }
      src += "  };\n";
    }
    if(!env.icons.empty()) {
      src += "  const char* const ICONS[] = {";
      for(size_t i = 0; i < env.icons.size(); ++i) src += (i ? ", " : "") + cString(env.icons[i]);
      src += "};\n";
    }
    // voice clips: assets/voice/<language code>/<key>.wav
    struct Voice { int lang; uint32_t key; uint32_t asset; std::string path; };
    std::vector<Voice> voices{};
    for(const auto &entry : project.getAssets().getTypeEntries(Project::FileType::AUDIO)) {
      const std::string &pp = entry.projectPath;
      if(!pp.starts_with("assets/voice/")) continue;
      const auto rest = pp.substr(13);
      const auto slash = rest.find('/');
      if(slash == std::string::npos) continue;
      const int lang = cfg.indexOf(rest.substr(0, slash));
      std::string key = rest.substr(slash + 1);
      key = key.substr(0, key.rfind('.'));
      auto idx = ctx.assetUUIDToIdx.find(entry.getUUID());
      if(lang < 0 || idx == ctx.assetUUIDToIdx.end() || !isValidKey(key)) continue;
      voices.push_back({lang, P64::Loc::keyId(key.data(), key.size()).hash, idx->second, pp});
    }
    std::sort(voices.begin(), voices.end(), [](const Voice &a, const Voice &b) { return std::tie(a.lang, a.key) < std::tie(b.lang, b.key); });
    if(!voices.empty()) {
      src += "  constexpr P64::Loc::VoiceClip VOICES[] = {\n";
      for(const auto &v : voices) src += std::format("    {{0x{:08X}u, {}, 0, {}}}, // {}\n", v.key, v.lang, v.asset, v.path);
      src += "  };\n";
    }
    const int iconSet = env.iconSet.empty() ? -1 : env.fontSetIndex(env.iconSet);
    src += std::format("\n  constexpr P64::Loc::ProjectData DATA{{\n    {}, {},\n    {}, {},\n    PALETTE, {},\n    EFFECTS, {},\n    {}, {},\n    {}, {}, {}, 0x{:X}u,\n    {},\n    {}, {}\n  }};\n}}\n\n",
      env.fontSets.empty() ? "nullptr" : "SETS", env.fontSets.size(),
      o.fonts.empty() ? "nullptr" : "FONTS", o.fonts.size(),
      env.paletteRgba.size(), env.effects.size(),
      env.controls.empty() ? "nullptr" : "CONTROLS", env.controls.size(),
      env.icons.empty() ? "nullptr" : "ICONS", env.icons.size(), iconSet < 0 ? 0xFF : iconSet, env.iconBase,
      o.fallbackColor, voices.empty() ? "nullptr" : "VOICES", voices.size());
    src += "void P64::Loc::Generated::registerLanguages()\n{\n"
           "  P64::Loc::setLanguageList(LANGUAGES, sizeof(LANGUAGES) / sizeof(LANGUAGES[0]));\n"
           "  P64::Loc::setProjectData(&DATA);\n}\n";
    Utils::FS::saveTextFileIfChanged(project.getPath() + "/src/p64/locTable.cpp", src);
  }

  /** Bakes one font with mkfont, unless the same inputs made it before. */
  bool bakeFont(Build::SceneCtx &ctx, Project::Project &project, const fs::path &fontPath, const FontSetDef &fs, int variant,
                int size, const std::string &charset, const fs::path &outFile, bool bmfont, const std::string &range)
  {
    const fs::path mkFont = fs::path{project.conf.pathN64Inst} / "bin" / "mkfont";
    const fs::path work = fs::path{project.getPath()} / "build" / "loc" / outFile.stem();
    fs::create_directories(work);

    std::string cmd = "\"" + mkFont.string() + "\" -c 1 -o \"" + work.string() + "\"";
    if(!bmfont) {
      cmd += std::format(" -s {}", size);
      if(fs.outline > 0.0f) cmd += std::format(" --outline {:.2f}", fs.outline);
      if(fs.monochrome) cmd += " --monochrome";
      for(const auto &axis : fs.varAxis[variant]) cmd += " --var-axis " + axis;
      if(!range.empty()) {
        cmd += " --range " + range;
      } else {
        Utils::FS::saveTextFileIfChanged(work / "charset.txt", charset);
        cmd += " --charset \"" + (work / "charset.txt").string() + "\"";
      }
    }
    if(bmfont) {
      // mkfont wants an ellipsis glyph; a bitmap font of icons has no '.': name its first glyph
      static const std::regex firstChar{R"(char\s+id=(\d+))"};
      std::smatch m;
      const std::string fnt = Utils::FS::loadTextFile(fontPath);
      if(std::regex_search(fnt, m, firstChar)) cmd += std::format(" --ellipsis {:X},1", std::stoul(m[1].str()));
    }
    cmd += " \"" + fontPath.string() + "\"";

    // the inputs: the command, the characters, and the font file's size and time
    std::error_code ec{};
    const auto fontTime = fs::last_write_time(fontPath, ec).time_since_epoch().count();
    const std::string sig = cmd + "\n" + charset + "\n" + std::to_string(fs::file_size(fontPath, ec)) + ":" + std::to_string(fontTime);
    const fs::path sigFile = work / "inputs.sig"; // not next to the font: all of filesystem/ goes into the ROM
    if(fs::exists(outFile) && Utils::FS::loadTextFile(sigFile) == sig) return true;

    Utils::Logger::log("Localization: baking font " + outFile.filename().string());
    if(!ctx.toolchain.runCmdSyncLogged(cmd)) return false;
    const fs::path made = work / (fontPath.stem().string() + ".font64");
    if(!fs::exists(made)) {
      Utils::Logger::log("Localization: mkfont made no " + made.string(), Utils::Logger::LEVEL_ERROR);
      return false;
    }
    fs::create_directories(outFile.parent_path());
    fs::copy_file(made, outFile, fs::copy_options::overwrite_existing, ec);
    fs::remove(made, ec);
    Utils::FS::saveTextFile(sigFile, sig);
    return !ec;
  }
}

bool Build::buildLocalization(Project::Project &project, SceneCtx &ctx)
{
  if(!hasLocalization(project.getPath())) {
    writeGenerated(project, ctx, nullptr);
    return true;
  }

  Localization loc{};
  std::vector<std::string> errors{};
  load(project.getPath(), loc, errors);

  // Refresh: every key the project uses is in the tables before they are written
  auto uses = scanProject(project, errors);
  for(const auto &[key, list] : ctx.locUses) {
    auto &dst = uses[key];
    dst.insert(dst.end(), list.begin(), list.end());
  }
  auto added = loc.addMissing(uses);
  if(!added.empty()) {
    Utils::Logger::log(std::format("Localization: added {} new key(s) to the tables", added.size()));
    for(const auto &key : added) Utils::Logger::log("  + " + key);
  }
  // also when only a translation table lacked a key the source has: files that did not change are not written
  save(project.getPath(), loc);

  const auto vars = scanVariables(project, errors);
  CompileEnv env = makeCompileEnv(loc.config, vars, errors);

  // two keys with the same id cannot be told apart at runtime
  std::map<uint32_t, std::string> ids{};
  for(const auto &key : loc.keys()) {
    const uint32_t id = P64::Loc::keyId(key.data(), key.size()).hash;
    auto [it, inserted] = ids.emplace(id, key);
    if(!inserted) {
      errors.push_back(std::format("the keys '{}' and '{}' have the same id {:08X}; rename one of them", it->second, key, id));
    }
  }

  // compile every text of every language
  const auto *src = loc.source();
  const auto &cfg = loc.config;
  const auto *srcLang = cfg.find(cfg.source);
  std::vector<std::vector<BuiltEntry>> tables(cfg.languages.size());
  std::vector<std::set<uint32_t>> glyphs(env.fontSets.size());
  for(size_t li = 0; li < cfg.languages.size(); ++li) {
    const auto &lang = cfg.languages[li];
    const auto rule = pluralRuleOf((lang.pseudo && srcLang) ? *srcLang : lang);
    const int setIdx = env.fontSetIndex(lang.fonts);
    int missing = 0, review = 0;
    for(const auto &[id, key] : ids) {
      std::string srcText{};
      if(src) {
        auto it = src->entries.find(key);
        if(it != src->entries.end()) srcText = it->second.text;
      }
      BuiltEntry e{.id = id};
      std::string text{};
      if(lang.pseudo) {
        text = pseudoText(srcText);
      } else {
        const auto &table = loc.tables[li];
        auto it = table.entries.find(key);
        if(it != table.entries.end() && !it->second.text.empty()) {
          text = it->second.text;
        } else {
          text = srcText;
          e.flags |= P64::Loc::ENTRY_FALLBACK;
        }
        switch(loc.status(key, li)) {
          case Status::Missing: case Status::MissingSource: ++missing; break;
          case Status::NeedsReview: ++review; break;
          case Status::Ok: break;
        }
      }
      std::vector<std::string> textErrors{};
      e.text = compileText(text, env, rule, textErrors);
      for(const auto &err : textErrors) errors.push_back(lang.code + ": " + key + ": " + err);

      if(setIdx >= 0) collectGlyphs(e.text, glyphs[setIdx]);
      if(usesOtherFontSets(e.text)) for(auto &g : glyphs) collectGlyphs(e.text, g);
      tables[li].push_back(std::move(e));
    }
    if(!lang.pseudo && (missing || review)) {
      Utils::Logger::log(std::format("Localization: {}: {} key(s), {} missing, {} to review", lang.code, ids.size(), missing, review),
        static_cast<int>(li) == loc.sourceIndex() ? Utils::Logger::LEVEL_WARN : Utils::Logger::LEVEL_INFO);
    }
  }

  for(const auto &err : errors) Utils::Logger::log("Localization: " + err, Utils::Logger::LEVEL_ERROR);
  if(!errors.empty()) return false;

  // the debug tint of untranslated text
  Output out{.loc = &loc, .env = &env};
  {
    int idx = env.paletteIndex("_fallback");
    if(idx < 0 && env.palette.size() < 32) {
      env.palette.push_back("_fallback");
      env.paletteRgba.push_back(0xFF60FFFFu);
      idx = static_cast<int>(env.palette.size()) - 1;
    }
    out.fallbackColor = static_cast<uint8_t>(idx < 0 ? 0 : idx);
  }

  const fs::path outDir = fs::path{project.getPath()} / "filesystem" / "p64" / "loc";
  fs::create_directories(outDir);
  for(size_t li = 0; li < cfg.languages.size(); ++li) {
    const auto &lang = cfg.languages[li];
    const auto bytes = writeTable(std::move(tables[li]), lang.pseudo ? P64::Loc::FILE_PSEUDO : 0);
    saveBinaryIfChanged(outDir / (lang.code + ".loc"), bytes);
    ctx.files.push_back("filesystem/p64/loc/" + lang.code + ".loc");
  }

  // fonts: every variant and size of every set, with the characters the texts use
  const fs::path localeDirPath = localeDir(project.getPath());
  out.ids.assign(env.fontSets.size(), {});
  uint8_t nextId = FIRST_FONT_ID;
  const int iconSetIdx = env.iconSet.empty() ? -1 : env.fontSetIndex(env.iconSet);
  for(size_t s = 0; s < env.fontSets.size(); ++s) {
    const auto &fs = env.fontSets[s];
    const bool isIcons = static_cast<int>(s) == iconSetIdx;
    std::set<uint32_t> chars = glyphs[s];
    for(uint32_t c = 0x20; c < 0x7F; ++c) chars.insert(c); // digits and what Text variables may show
    const std::string charset = utf8Of(chars);
    const std::string range = isIcons && !env.icons.empty()
      ? std::format("{:X}-{:X}", env.iconBase, env.iconBase + env.icons.size() - 1) : std::string{};

    for(int v = 0; v < 4; ++v) {
      if(fs.files[v].empty()) continue;
      const fs::path fontPath = localeDirPath / fs.files[v];
      if(!fs::exists(fontPath)) {
        Utils::Logger::log("Localization: font set '" + fs.name + "' " + VARIANT_NAMES[v] + ": " + fontPath.string() + " does not exist",
          Utils::Logger::LEVEL_ERROR);
        return false;
      }
      const bool bmfont = fontPath.extension() == ".fnt";
      for(size_t z = 0; z < fs.sizes.size(); ++z) {
        if(bmfont && z > 0) break; // a bitmap font has one size
        if(nextId == 0xFF) {
          Utils::Logger::log("Localization: too many fonts (rdpq allows 255 font ids)", Utils::Logger::LEVEL_ERROR);
          return false;
        }
        const std::string name = std::format("{}_{}_{}.font64", fs.name, v, z);
        if(!bakeFont(ctx, project, fontPath, fs, v, fs.sizes[z], charset, outDir / "f" / name, bmfont, range)) {
          Utils::Logger::log("Localization: baking " + name + " failed", Utils::Logger::LEVEL_ERROR);
          return false;
        }
        ctx.files.push_back("filesystem/p64/loc/f/" + name);
        out.ids[s][v][z] = nextId;
        out.fonts.push_back({"rom:/p64/loc/f/" + name, nextId, static_cast<uint8_t>(s), static_cast<uint8_t>(v), isIcons});
        ++nextId;
      }
      if(bmfont) for(size_t z = 1; z < fs.sizes.size(); ++z) out.ids[s][v][z] = out.ids[s][v][0];
    }
  }

  writeGenerated(project, ctx, &out);
  return true;
}
