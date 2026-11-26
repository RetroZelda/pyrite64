/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: dialog trees (docs/todo/dialog/PLAN.md, section 7.7). Builds every .p64dlg into the engine's
// format (.dlg) and writes the speakers list (src/p64/dialogTable.cpp).
#include "projectBuilder.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>

#include "../project/project.h"
#include "../project/dialog/dialogTree.h"
#include "../project/dialog/speakers.h"
#include "../project/locale/locProject.h"
#include "../utils/fs.h"
#include "../utils/json.h"
#include "../utils/logger.h"
#include "../utils/string.h"
#include "../../n64/engine/include/text/locId.h"

namespace fs = std::filesystem;

namespace
{
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

  uint32_t parseColor(const std::string &s)
  {
    if(s.size() != 7 && s.size() != 9) return 0xFFFFFFFFu;
    try {
      uint32_t v = static_cast<uint32_t>(std::stoul(s.substr(1), nullptr, 16));
      return s.size() == 7 ? (v << 8) | 0xFF : v;
    } catch(...) {
      return 0xFFFFFFFFu;
    }
  }

  /** A condition script's values, the way a Code component writes them (compCode.cpp build). */
  bool writeConditionArgs(Project::Project &project, Build::SceneCtx &ctx, uint64_t scriptUuid,
                          const std::map<std::string, std::string> &args, std::vector<uint8_t> &out, std::string &error)
  {
    auto *script = project.getAssets().getEntryByUUID(scriptUuid);
    if(!script) { error = "the condition script does not exist"; return false; }
    Utils::BinaryFile f{};
    for(const auto &field : script->params.fields) {
      auto it = args.find(field.name);
      std::string val = it == args.end() ? std::string{} : it->second;
      if(val.empty()) val = field.defaultValue;
      if(val.empty() && field.type != Utils::DataType::string) val = "0"; // an empty text stays empty
      try {
        switch(field.type) {
          case Utils::DataType::ASSET_SPRITE: case Utils::DataType::ASSET_EMITTER: case Utils::DataType::PREFAB:
            f.write<uint32_t>(ctx.assetUUIDToIdx[Utils::parseU64(val)]);
            break;
          case Utils::DataType::OBJECT_REF:
            f.write<uint32_t>(0); // a tree has no objects of its own
            break;
          case Utils::DataType::LOC_TEXT:
            f.write<uint32_t>(val == "0" ? 0 : P64::Loc::keyId(val.data(), val.size()).hash);
            break;
          case Utils::DataType::string:
            for(uint32_t i = 0; i < field.dataSize; ++i) f.write<uint8_t>((i + 1 < field.dataSize && i < val.size()) ? uint8_t(val[i]) : 0);
            break;
          default:
            f.writeAs(val, field.type);
            break;
        }
      } catch(...) {
        error = "'" + field.name + "': '" + val + "' is not a valid value";
        return false;
      }
    }
    out = f.getData();
    out.resize(f.getSize());
    return true;
  }

  void writeSpeakerTable(Project::Project &project, Build::SceneCtx &ctx, const Project::Dialog::Speakers &speakers,
                         const Project::Loc::CompileEnv *locEnv)
  {
    std::string src = "// NOTE: Auto-Generated File!\n// Fork: dialog trees, the project's speakers (locale/speakers.json).\n\n"
                      "#include \"dialog/dialog.h\"\n\n";
    if(speakers.list.empty()) {
      src += "void P64::Dialog::Generated::registerSpeakers() {}\n";
      Utils::FS::saveTextFileIfChanged(project.getPath() + "/src/p64/dialogTable.cpp", src);
      return;
    }
    src += "namespace\n{\n";
    for(size_t i = 0; i < speakers.list.size(); ++i) {
      const auto &s = speakers.list[i];
      if(s.portraits.empty()) continue;
      src += std::format("  constexpr P64::Dialog::Portrait PORTRAITS_{}[] = {{", i);
      for(const auto &[name, asset] : s.portraits) {
        auto it = ctx.assetUUIDToIdx.find(asset);
        src += std::format("{{{}, {}}}, ", cString(name), it == ctx.assetUUIDToIdx.end() ? std::string{"P64::Dialog::NO_ASSET"} : std::to_string(it->second));
      }
      src += "};\n";
    }
    src += "  constexpr P64::Dialog::Speaker SPEAKERS[] = {\n";
    for(size_t i = 0; i < speakers.list.size(); ++i) {
      const auto &s = speakers.list[i];
      const int fontSet = (locEnv && !s.fonts.empty()) ? locEnv->fontSetIndex(s.fonts) : -1;
      auto blip = ctx.assetUUIDToIdx.find(s.blipSound);
      src += std::format("    {{{}, 0x{:08X}u, P64::Dialog::Lang::{}, {}, {}, {}, 0x{:08X}u, {}, 0, {:.3f}f, {}, {}}},\n",
        cString(s.id), P64::Loc::keyId(s.name.data(), s.name.size()).hash, s.language == "paired" ? "Paired" : "Game",
        fontSet < 0 ? 0xFF : fontSet, std::clamp(s.blipEvery, 0, 255), s.portraits.size(), parseColor(s.nameColor),
        (s.blipSound == 0 || blip == ctx.assetUUIDToIdx.end()) ? std::string{"P64::Dialog::NO_ASSET"} : std::to_string(blip->second),
        s.blipPitch, cString(s.talkClip), s.portraits.empty() ? std::string{"nullptr"} : std::format("PORTRAITS_{}", i));
    }
    src += "  };\n}\n\nvoid P64::Dialog::Generated::registerSpeakers()\n{\n"
           "  P64::Dialog::setSpeakers(SPEAKERS, sizeof(SPEAKERS) / sizeof(SPEAKERS[0]));\n}\n";
    Utils::FS::saveTextFileIfChanged(project.getPath() + "/src/p64/dialogTable.cpp", src);
  }
}

bool Build::buildDialogAssets(Project::Project &project, SceneCtx &ctx)
{
  std::vector<std::string> errors{};
  Project::Dialog::Speakers speakers{};
  Project::Dialog::loadSpeakers(project.getPath(), speakers, errors);

  // the font sets, for a speaker's own font set
  std::optional<Project::Loc::CompileEnv> locEnv{};
  if(Project::Loc::hasLocalization(project.getPath())) {
    Project::Loc::Localization loc{};
    std::vector<std::string> locErrors{};
    Project::Loc::load(project.getPath(), loc, locErrors);
    locEnv = Project::Loc::makeCompileEnv(loc.config, {}, locErrors);
    for(const auto &s : speakers.list) {
      if(!s.fonts.empty() && locEnv->fontSetIndex(s.fonts) < 0) errors.push_back("speakers.json: '" + s.id + "' uses the font set '" + s.fonts + "', which does not exist");
    }
  }
  writeSpeakerTable(project, ctx, speakers, locEnv ? &*locEnv : nullptr);

  Project::Dialog::BuildEnv env{};
  env.scriptIndex = [&](uint64_t uuid) -> int {
    auto *script = project.getAssets().getEntryByUUID(uuid);
    if(!script || !script->condition.isCondition) return -1;
    auto it = ctx.codeIdxMapUUID.find(uuid);
    return it == ctx.codeIdxMapUUID.end() ? -1 : static_cast<int>(it->second);
  };
  env.writeArgs = [&](uint64_t uuid, const std::map<std::string, std::string> &args, std::vector<uint8_t> &out, std::string &error) {
    return writeConditionArgs(project, ctx, uuid, args, out, error);
  };
  env.speakerIndex = [&](const std::string &id) { return speakers.indexOf(id); };
  env.portraitIndex = [&](const std::string &speaker, const std::string &portrait) -> int {
    const auto *s = speakers.find(speaker);
    if(!s) return -1;
    for(size_t i = 0; i < s->portraits.size(); ++i) if(s->portraits[i].first == portrait) return static_cast<int>(i);
    return -1;
  };
  env.treeAssetIndex = [&](uint64_t uuid) -> int {
    auto it = ctx.assetUUIDToIdx.find(uuid);
    return it == ctx.assetUUIDToIdx.end() ? -1 : static_cast<int>(it->second);
  };

  for(const auto &entry : project.getAssets().getTypeEntries(Project::FileType::DIALOG_TREE)) {
    if(entry.conf.exclude) continue;
    Project::Dialog::Tree tree{};
    std::vector<std::string> treeErrors{}, warnings{};
    try {
      Project::Dialog::parse(Utils::JSON::loadFile(entry.path), tree, treeErrors);
    } catch(const std::exception &e) {
      treeErrors.push_back(e.what());
    }
    auto bytes = Project::Dialog::build(tree, env, treeErrors, warnings);
    for(const auto &w : warnings) Utils::Logger::log("Dialog " + entry.projectPath + ": " + w, Utils::Logger::LEVEL_WARN);
    for(const auto &e : treeErrors) errors.push_back(entry.projectPath + ": " + e);
    if(!treeErrors.empty()) continue;

    const fs::path out = fs::path{project.getPath()} / entry.outPath;
    fs::create_directories(out.parent_path());
    Utils::FS::saveTextFileIfChanged(out, std::string(bytes.begin(), bytes.end()));
    ctx.files.push_back(Utils::FS::toUnixPath(entry.outPath));
  }

  for(const auto &e : errors) Utils::Logger::log("Dialog: " + e, Utils::Logger::LEVEL_ERROR);
  return errors.empty();
}
