/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, section 11, phase 7). An estimate of how many
// pages a text takes in the game's talk box, measured with the language's own font (the TTF or OTF
// of its font set, at its normal size), so the Strings window can flag a translation that runs
// longer than the source. Tags are left out and variables count as nothing: it is an estimate.
#pragma once
#include <optional>
#include <string>

#include "locCompile.h"

namespace Project::Loc
{
  struct PageEstimate
  {
    int lines{0};
    int pages{0};
  };

  /**
   * The box the estimate fits text into: locale.json "box" {"width": 272, "lines": 3,
   * "pageSizeStep": 1}. pageSizeStep is the text size, in font sizes from the normal one, that the
   * game works its pages out at (Kentucky Castle: its largest text size, so every size shows the
   * same pages).
   */
  struct BoxSize
  {
    float width{272.0f};
    int lines{3};
    int pageSizeStep{0};
  };
  BoxSize boxSizeOf(const Config &config);

  /** Pages of a text in a language; nothing when its font cannot be read. 'localeDir' is <project>/locale. */
  std::optional<PageEstimate> estimatePages(const std::string &localeDir, CompileEnv &env, const Language &lang,
                                            const std::string &text, const BoxSize &box);
}
