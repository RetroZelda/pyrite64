/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization, the page estimate of the Strings window.
#include "locMeasure.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

#include "../../utils/fs.h"
#include "../../../n64/engine/include/text/locPages.h"

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

  struct FitBox
  {
    LoadedFont* font;
    float scale;
    float width;
    int lines;
    bool byChar;
  };

  float advanceOf(const FitBox &box, uint32_t cp)
  {
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&box.font->info, int(cp), &adv, &lsb);
    return float(adv) * box.scale;
  }

  /**
   * Lays [from, end) out from the top of the box like rdpq: words move to the next line whole (any
   * letter does, for languages wrapped by character). Returns the bytes that fit in 'maxLines';
   * 'linesUsed' gets the lines they take.
   */
  int layout(const char* t, int from, int end, const FitBox &box, int maxLines, int &linesUsed)
  {
    int line = 1, wordStart = from;
    float x = 0.0f, word = 0.0f; // word: the word being built, it moves to the next line whole
    for(int i = from; i < end && t[i];) {
      uint32_t cp = 0;
      int next = i;
      if((t[i] == '$' || t[i] == '^') && i + 1 < end && t[i + 1] == t[i]) {
        cp = uint8_t(t[i]);
        next = i + 2;
      } else {
        const char* p = t + i;
        cp = P64::Loc::utf8Decode(p);
        next = int(p - t);
      }
      if(cp == '\n') {
        if(line + 1 > maxLines) { linesUsed = line; return i - from; }
        ++line;
        x = 0.0f; word = 0.0f; wordStart = next; i = next;
        continue;
      }
      const float w = advanceOf(box, cp);
      if(box.byChar) {
        if(x + w > box.width && x > 0.0f) {
          if(line + 1 > maxLines) { linesUsed = line; return i - from; }
          ++line;
          x = 0.0f;
        }
        x += w; i = next;
        continue;
      }
      if(cp == ' ') { x += word + w; word = 0.0f; wordStart = next; i = next; continue; }
      word += w;
      if(x + word > box.width && x > 0.0f) {
        if(line + 1 > maxLines) { linesUsed = line; return wordStart - from; }
        ++line;
        x = 0.0f;
      }
      i = next;
    }
    linesUsed = line;
    return end - from;
  }

  int fitPage(const char* text, int from, int end, void* user)
  {
    const auto &box = *static_cast<const FitBox*>(user);
    int lines = 0;
    return layout(text, from, end, box, box.lines, lines);
  }
}

Project::Loc::BoxSize Project::Loc::boxSizeOf(const Config &config)
{
  BoxSize box{};
  if(config.raw.contains("box") && config.raw["box"].is_object()) {
    box.width = config.raw["box"].value("width", box.width);
    box.lines = config.raw["box"].value("lines", box.lines);
    box.pageSizeStep = config.raw["box"].value("pageSizeStep", box.pageSizeStep);
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
  // the size the game works pages out at
  const int sizePx = fs.sizes.empty() ? 16 : fs.sizes[size_t(std::clamp(fs.base + box.pageSizeStep, 0, int(fs.sizes.size()) - 1))];
  // mkfont sizes by the em, like FreeType
  const float scale = stbtt_ScaleForMappingEmToPixels(&font->info, float(sizePx));

  std::vector<std::string> errors{};
  const std::string compiled = compileText(text, env, pluralRuleOf(lang), errors);

  // the text as the box shows it: letters, spaces and line breaks; tokens and variables left out,
  // a [page] starts a new segment
  std::vector<std::string> segments{std::string{}};
  const char* p = compiled.c_str();
  while(*p) {
    if((p[0] == '$' || p[0] == '^') && p[1] == p[0]) { segments.back().append(p, 2); p += 2; continue; }
    const char* at = p;
    const uint32_t cp = P64::Loc::utf8Decode(p);
    if(P64::Loc::isToken(cp)) continue;
    if(P64::Loc::isControl(cp)) {
      if(P64::Loc::codeOp(cp) == P64::Loc::CTRL_BUILTIN && P64::Loc::codeArg(cp) == P64::Loc::CTRL_PAGE) segments.emplace_back();
      continue;
    }
    segments.back().append(at, size_t(p - at));
  }

  // the pages break where the game breaks them (text/locPages.h), with this font's widths
  FitBox fitBox{font, scale, box.width, std::max(1, box.lines), lang.wrap == "char"};
  PageEstimate est{};
  for(const auto &seg : segments) {
    P64::Loc::PageSpan spans[64]{};
    const int n = P64::Loc::splitPages(seg.c_str(), 0, int(seg.size()), fitPage, &fitBox, fitBox.byChar, spans, 64);
    for(int i = 0; i < n; ++i) {
      int lines = 0;
      layout(seg.c_str(), spans[i].start, spans[i].start + spans[i].len, fitBox, INT_MAX, lines);
      est.lines += lines;
      ++est.pages;
    }
  }
  return est;
}
