/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization, the page estimate of the Strings window.
#include "locMeasure.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

#include "../../utils/fs.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "imstb_truetype.h"

namespace
{
  struct LoadedFont
  {
    std::string data{};
    stbtt_fontinfo info{};
    bool ok{false};
  };

  LoadedFont* fontOf(const std::string &path)
  {
    static std::map<std::string, std::unique_ptr<LoadedFont>> cache{};
    auto &slot = cache[path];
    if(!slot) {
      slot = std::make_unique<LoadedFont>();
      slot->data = Utils::FS::loadTextFile(path);
      if(!slot->data.empty()) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(slot->data.data());
        slot->ok = stbtt_InitFont(&slot->info, bytes, stbtt_GetFontOffsetForIndex(bytes, 0)) != 0;
      }
    }
    return slot->ok ? slot.get() : nullptr;
  }
}

Project::Loc::BoxSize Project::Loc::boxSizeOf(const Config &config)
{
  BoxSize box{};
  if(config.raw.contains("box") && config.raw["box"].is_object()) {
    box.width = config.raw["box"].value("width", box.width);
    box.lines = config.raw["box"].value("lines", box.lines);
  }
  return box;
}

std::optional<Project::Loc::PageEstimate> Project::Loc::estimatePages(const std::string &localeDir, CompileEnv &env, const Language &lang,
                                                                       const std::string &text, const BoxSize &box)
{
  const int set = env.fontSetIndex(lang.fonts);
  if(set < 0) return std::nullopt;
  const auto &fs = env.fontSets[size_t(set)];
  if(fs.files[0].empty() || fs.files[0].ends_with(".fnt")) return std::nullopt;
  LoadedFont* font = fontOf((std::filesystem::path{localeDir} / fs.files[0]).string());
  if(!font) return std::nullopt;
  const int sizePx = fs.sizes.empty() ? 16 : fs.sizes[size_t(std::clamp(fs.base, 0, int(fs.sizes.size()) - 1))];
  // mkfont sizes by the em, like FreeType
  const float scale = stbtt_ScaleForMappingEmToPixels(&font->info, float(sizePx));

  std::vector<std::string> errors{};
  const std::string compiled = compileText(text, env, pluralRuleOf(lang), errors);

  // the letters in order: codes, tokens and variables left out, line breaks kept
  std::vector<uint32_t> cps{};
  const char* p = compiled.c_str();
  while(*p) {
    if((p[0] == '$' || p[0] == '^') && p[1] == p[0]) { cps.push_back(uint32_t(uint8_t(p[0]))); p += 2; continue; }
    const uint32_t cp = P64::Loc::utf8Decode(p);
    if(P64::Loc::isToken(cp)) continue;
    if(P64::Loc::isControl(cp)) {
      if(P64::Loc::codeOp(cp) == P64::Loc::CTRL_BUILTIN && P64::Loc::codeArg(cp) == P64::Loc::CTRL_PAGE) cps.push_back(0xFFFF); // a page break
      continue;
    }
    cps.push_back(cp);
  }

  auto advance = [&](uint32_t cp) {
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&font->info, int(cp), &adv, &lsb);
    return float(adv) * scale;
  };

  const bool byChar = lang.wrap == "char";
  const int perPage = std::max(1, box.lines);
  PageEstimate est{};
  auto finishSegment = [&](int lines) {
    est.lines += lines;
    est.pages += (lines + perPage - 1) / perPage;
  };
  int lines = 1;
  float x = 0.0f;
  float word = 0.0f; // the word being built (word wrap): it moves to the next line whole
  for(uint32_t cp : cps) {
    if(cp == 0xFFFF) { finishSegment(lines); lines = 1; x = 0.0f; word = 0.0f; continue; } // [page]
    if(cp == '\n') { ++lines; x = 0.0f; word = 0.0f; continue; }
    const float w = advance(cp);
    if(byChar) {
      if(x + w > box.width && x > 0.0f) { ++lines; x = 0.0f; }
      x += w;
      continue;
    }
    if(cp == ' ') { x += word + w; word = 0.0f; continue; }
    word += w;
    if(x + word > box.width && x > 0.0f) { ++lines; x = 0.0f; }
  }
  finishSegment(lines);
  return est;
}
