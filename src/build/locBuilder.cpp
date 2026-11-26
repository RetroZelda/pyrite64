/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, section 4.8). Writes one string table per language
// (filesystem/p64/loc/<code>.loc) and the list of languages (src/p64/locTable.cpp).
#include "projectBuilder.h"

#include <filesystem>
#include <format>
#include <map>

#include "../project/project.h"
#include "../project/locale/locProject.h"
#include "../utils/fs.h"
#include "../utils/logger.h"
#include "../editor/imgui/notification.h"
#include "../../n64/engine/include/text/locId.h"
#include "../../n64/engine/include/text/locTable.h"

namespace fs = std::filesystem;

namespace
{
  std::string cString(const std::string &s)
  {
    std::string out = "\"";
    for(unsigned char c : s) {
      if(c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
      else if(c < 0x20) out += std::format("\\x{:02x}", c);
      else out += static_cast<char>(c);
    }
    return out + "\"";
  }

  bool saveBinaryIfChanged(const fs::path &path, const std::vector<uint8_t> &data)
  {
    std::string bytes(data.begin(), data.end());
    return Utils::FS::saveTextFileIfChanged(path, bytes);
  }

  void writeLanguageList(Project::Project &project, const Project::Loc::Localization *loc)
  {
    std::string src = "// NOTE: Auto-Generated File!\n// Fork: localization, the project's languages (locale/locale.json).\n\n"
                      "#include \"text/loc.h\"\n\n";
    if(!loc || loc->config.languages.empty()) {
      src += "void P64::Loc::Generated::registerLanguages() {}\n";
    } else {
      const auto &cfg = loc->config;
      src += "namespace\n{\n  constexpr P64::Loc::Language LANGUAGES[] = {\n";
      for(const auto &lang : cfg.languages) {
        const int pair = cfg.indexOf(cfg.pairOf(lang.code));
        uint8_t flags = 0;
        if(lang.pseudo) flags |= 1 << 0;           // LANG_PSEUDO
        if(lang.wrap == "char") flags |= 1 << 1;   // LANG_WRAP_CHAR
        src += std::format("    {{{}, {}, {}, {}, {}, 0, 0, {:.3f}f}},\n",
          cString(lang.code), cString(lang.name), cString("rom:/p64/loc/" + lang.code + ".loc"),
          pair < 0 ? std::string{"P64::Loc::NO_LANGUAGE"} : std::to_string(pair), flags, lang.revealScale);
      }
      src += "  };\n}\n\nvoid P64::Loc::Generated::registerLanguages()\n{\n"
             "  P64::Loc::setLanguageList(LANGUAGES, sizeof(LANGUAGES) / sizeof(LANGUAGES[0]));\n}\n";
    }
    Utils::FS::saveTextFileIfChanged(project.getPath() + "/src/p64/locTable.cpp", src);
  }
}

bool Build::buildLocalization(Project::Project &project, SceneCtx &ctx)
{
  using namespace Project::Loc;
  if(!hasLocalization(project.getPath())) {
    writeLanguageList(project, nullptr);
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
    save(project.getPath(), loc);
  }

  bool ok = true;
  for(const auto &err : errors) {
    Utils::Logger::log("Localization: " + err, Utils::Logger::LEVEL_ERROR);
    ok = false;
  }

  // two keys with the same id cannot be told apart at runtime
  std::map<uint32_t, std::string> ids{};
  for(const auto &key : loc.keys()) {
    const uint32_t id = P64::Loc::keyId(key.data(), key.size()).hash;
    auto [it, inserted] = ids.emplace(id, key);
    if(!inserted) {
      Utils::Logger::log(std::format("Localization: the keys '{}' and '{}' have the same id {:08X}; rename one of them",
        it->second, key, id), Utils::Logger::LEVEL_ERROR);
      ok = false;
    }
  }
  if(!ok) return false;

  const auto *src = loc.source();
  const fs::path outDir = fs::path{project.getPath()} / "filesystem" / "p64" / "loc";
  fs::create_directories(outDir);

  for(size_t li = 0; li < loc.config.languages.size(); ++li) {
    const auto &lang = loc.config.languages[li];
    std::vector<BuiltEntry> entries{};
    int missing = 0, review = 0;
    for(const auto &[id, key] : ids) {
      const Entry *srcEntry = nullptr;
      if(src) {
        auto it = src->entries.find(key);
        if(it != src->entries.end()) srcEntry = &it->second;
      }
      const std::string srcText = srcEntry ? srcEntry->text : std::string{};

      BuiltEntry e{.id = id};
      if(lang.pseudo) {
        e.text = pseudoText(srcText);
      } else {
        const auto &table = loc.tables[li];
        auto it = table.entries.find(key);
        const bool has = it != table.entries.end() && !it->second.text.empty();
        if(has) {
          e.text = it->second.text;
        } else {
          e.text = srcText;
          e.flags |= P64::Loc::ENTRY_FALLBACK;
        }
        switch(loc.status(key, li)) {
          case Status::Missing: case Status::MissingSource: ++missing; break;
          case Status::NeedsReview: ++review; break;
          case Status::Ok: break;
        }
      }
      e.text = escapeForRdpq(e.text); // phase 2 replaces this with the tag compiler
      entries.push_back(std::move(e));
    }

    const auto bytes = writeTable(std::move(entries), lang.pseudo ? P64::Loc::FILE_PSEUDO : 0);
    saveBinaryIfChanged(outDir / (lang.code + ".loc"), bytes);
    ctx.files.push_back("filesystem/p64/loc/" + lang.code + ".loc");

    if(lang.pseudo) continue;
    if(missing || review) {
      Utils::Logger::log(std::format("Localization: {}: {} key(s), {} missing, {} to review", lang.code, ids.size(), missing, review),
        static_cast<int>(li) == loc.sourceIndex() ? Utils::Logger::LEVEL_WARN : Utils::Logger::LEVEL_INFO);
    }
  }

  writeLanguageList(project, &loc);
  return true;
}
