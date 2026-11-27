/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog). Where the pages of a dialog line break: whole sentences
// stay on one page, and a sentence longer than a page is split evenly, at a comma or a line break
// when one is near the even point. The game lays the pages out with rdpq, the editor's page
// estimate with the font file; both decide the breaks here, so they agree.
// This header must stay free of engine and libdragon includes: the host unit tests include it.
#pragma once
#include <cstdint>

namespace P64::Loc
{
  /** A page of a text: the bytes [start, start + len). */
  struct PageSpan
  {
    int start;
    int len;
  };

  /**
   * How much of a text fits on one page: the bytes from 'from' (not past 'end') that the box holds.
   * The game asks rdpq, the editor measures the font. 0 means nothing fits; the page then takes
   * the rest of the text, like rdpq would show it.
   */
  using PageFitFn = int (*)(const char* text, int from, int end, void* user);

  /**
   * Splits [start, end) of formatted text, with no [page] in it, into pages:
   * - a page ends after the last sentence that fits on it whole; the next sentence starts the
   *   next page. A sentence ends at . ! ? (followed by a space or the end, in languages with
   *   spaces), at 。！？ and at any closing quote or bracket after them
   * - a sentence longer than a page is cut into pages of about the same length: at a comma
   *   (, ; : 、 ， ； ： … — or "...") or a line break near the even point when there is one, at a
   *   space (any letter that kinsoku allows, for languages wrapped by character) otherwise
   * The spaces and line breaks between pages belong to no page. Font and style codes ($xx, ^xx),
   * controls and tokens count as nothing. Writes at most 'maxPages' and at least one page (an
   * empty one for empty text); returns how many.
   */
  int splitPages(const char* text, int start, int end, PageFitFn fit, void* user, bool byChar,
                 PageSpan* out, int maxPages);
}
