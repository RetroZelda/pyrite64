/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization, the page breaks of dialog lines (text/locPages.h). Free of libdragon so the
// host unit tests (tests/locale.cpp) and the editor's page estimate run it too.
// relative includes: the editor builds this file too, without the engine's include folder
#include "../../include/text/locPages.h"
#include "../../include/text/locCodes.h"

namespace
{
  using namespace P64::Loc;

  // a comma is used when it lands this close to the even point: 4/10 of a page's share
  constexpr int SOFT_REACH_TENTHS = 4;
  // a sentence is measured in pages to split it evenly; more than this many is cut greedily
  constexpr int MAX_SENTENCE_PAGES = 64;

  struct Item
  {
    enum Kind : uint8_t { End, Glyph, Space, Newline, Code };
    Kind kind;
    uint32_t cp;
    int at;
    int next;
  };

  int hexValue(char h)
  {
    if(h >= '0' && h <= '9') return h - '0';
    if(h >= 'A' && h <= 'F') return h - 'A' + 10;
    if(h >= 'a' && h <= 'f') return h - 'a' + 10;
    return -1;
  }

  /** The item at byte 'i': a letter, a space, a line break, or a code that takes no room. */
  Item readItem(const char* t, int i, int end)
  {
    if(i >= end || t[i] == '\0') return {Item::End, 0, i, i};
    const char c = t[i];
    if(c == '$' || c == '^') {
      if(i + 1 < end && t[i + 1] == c) return {Item::Glyph, uint32_t(uint8_t(c)), i, i + 2}; // "$$" is a '$'
      if(i + 2 < end && hexValue(t[i + 1]) >= 0 && hexValue(t[i + 2]) >= 0) return {Item::Code, 0, i, i + 3};
    }
    if(c == ' ') return {Item::Space, ' ', i, i + 1};
    if(c == '\n') return {Item::Newline, '\n', i, i + 1};
    const char* p = t + i;
    const uint32_t cp = utf8Decode(p);
    int next = int(p - t);
    if(next > end) next = end;
    if(isControl(cp) || isToken(cp)) return {Item::Code, cp, i, next};
    return {Item::Glyph, cp, i, next};
  }

  int skipBlank(const char* t, int i, int end)
  {
    for(;;) {
      const Item it = readItem(t, i, end);
      if(it.kind != Item::Space && it.kind != Item::Newline) return i;
      i = it.next;
    }
  }

  /** Nothing but spaces, line breaks and codes from 'i' to the end. */
  bool restIsBlank(const char* t, int i, int end)
  {
    for(;;) {
      const Item it = readItem(t, i, end);
      if(it.kind == Item::End) return true;
      if(it.kind == Item::Glyph) return false;
      i = it.next;
    }
  }

  int countGlyphs(const char* t, int from, int to, int end, uint32_t* last = nullptr)
  {
    int n = 0;
    for(int i = from; i < to;) {
      const Item it = readItem(t, i, end);
      if(it.kind == Item::End) break;
      if(it.kind == Item::Glyph) {
        ++n;
        if(last) *last = it.cp;
      }
      i = it.next;
    }
    return n;
  }

  bool isTerminator(uint32_t cp)
  {
    switch(cp) {
      case '.': case '!': case '?':
      case 0x203C: case 0x2047: case 0x2048: case 0x2049: // ‼ ⁇ ⁈ ⁉
      case 0x3002: case 0xFF01: case 0xFF0E: case 0xFF1F: case 0xFF61: // 。！．？｡
        return true;
      default: return false;
    }
  }

  bool isComma(uint32_t cp)
  {
    switch(cp) {
      case ',': case ';': case ':':
      case 0x2014: case 0x2026: // — …
      case 0x3001: case 0xFF0C: case 0xFF1A: case 0xFF1B: case 0xFF64: // 、，：；､
        return true;
      default: return false;
    }
  }

  /** Quotes and brackets that close after the end of a sentence: they stay with it. */
  bool isClosing(uint32_t cp)
  {
    switch(cp) {
      case '"': case '\'': case ')': case ']': case '}':
      case 0x00BB: case 0x2019: case 0x201D: // » ’ ”
      case 0x3009: case 0x300B: case 0x300D: case 0x300F: case 0x3011: case 0x3015: case 0x3017:
      case 0x3019: case 0x301B: case 0xFF09: case 0xFF3D: case 0xFF5D: case 0xFF63: // 〉》」』】〕〗〙〛）］｝｣
        return true;
      default: return false;
    }
  }

  // the marks of languages without spaces (。、！) end what they end by themselves
  bool isWide(uint32_t cp) { return cp >= 0x3000; }

  bool isAsciiAlnum(uint32_t cp)
  {
    return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
  }

  enum class Brk : uint8_t { Sentence, Soft, Word };

  struct Mark
  {
    bool found;
    Brk kind;   // Sentence or Soft
    int next;   // after the marks and the closing quotes, brackets and codes after them
  };

  /**
   * At a letter: a run of sentence marks or commas starting there, and what it ends. "..." is a
   * comma (an ellipsis rarely ends a sentence). In languages with spaces, . ! ? and , need a space
   * (or the end) after them, and a sentence mark before a lowercase word is only a comma:
   * "3.5", "1,000", "e.g. this" and "Go!" he said do not end anything.
   */
  Mark readMark(const char* t, const Item &first, int end, bool byChar)
  {
    if(!isTerminator(first.cp) && !isComma(first.cp)) return {false, Brk::Soft, first.next};
    int dots = 0;
    bool stop = false, comma = false, wide = false;
    int j = first.at;
    for(;;) {
      const Item it = readItem(t, j, end);
      if(it.kind != Item::Glyph) break;
      if(it.cp == '.') ++dots;
      else if(isTerminator(it.cp)) stop = true;
      else if(isComma(it.cp)) comma = true;
      else break;
      if(isWide(it.cp)) wide = true;
      j = it.next;
    }
    Brk kind = (stop || (dots == 1 && !comma)) ? Brk::Sentence : Brk::Soft;
    for(;;) {
      const Item it = readItem(t, j, end);
      if(it.kind == Item::Code || (it.kind == Item::Glyph && isClosing(it.cp))) j = it.next;
      else break;
    }
    if(wide) return {true, kind, j};

    const Item after = readItem(t, j, end);
    const bool spaced = after.kind == Item::End || after.kind == Item::Space || after.kind == Item::Newline;
    if(!spaced) {
      // no space after it: in a language without spaces only "3.5" or "a,b" stop it
      if(!byChar || (after.kind == Item::Glyph && isAsciiAlnum(after.cp))) return {false, Brk::Soft, first.next};
      return {true, kind, j};
    }
    if(kind == Brk::Sentence && !byChar) {
      Item next = after;
      for(int k = after.next; next.kind == Item::Space || next.kind == Item::Newline || next.kind == Item::Code; k = next.next) next = readItem(t, k, end);
      if(next.kind == Item::Glyph && next.cp >= 'a' && next.cp <= 'z') kind = Brk::Soft;
    }
    return {true, kind, j};
  }

  /**
   * Walks [from, limit) and calls visit(kind, pos, letters before pos) for every place a page may
   * end: after a sentence (Sentence) or a comma (Soft), before a line break (Soft), before a space
   * (Word), and, for languages wrapped by character, between two letters kinsoku allows (Word).
   * A mark whose run goes past 'limit' is not offered. visit returns false to stop.
   */
  template<typename F>
  void walk(const char* t, int from, int limit, int end, bool byChar, F &&visit)
  {
    int glyphs = 0;
    uint32_t prev = 0;
    for(int i = from; i < limit;) {
      const Item it = readItem(t, i, end);
      if(it.kind == Item::End) return;
      if(it.kind == Item::Code) { i = it.next; continue; }
      if(it.kind == Item::Space || it.kind == Item::Newline) {
        if(glyphs > 0 && !visit(it.kind == Item::Space ? Brk::Word : Brk::Soft, it.at, glyphs)) return;
        i = it.next;
        continue;
      }
      if(byChar && glyphs > 0 && !kinsokuNoStart(it.cp) && !kinsokuNoEnd(prev)) {
        if(!visit(Brk::Word, it.at, glyphs)) return;
      }
      const Mark m = readMark(t, it, end, byChar);
      if(!m.found) {
        ++glyphs;
        prev = it.cp;
        i = it.next;
        continue;
      }
      glyphs += countGlyphs(t, it.at, m.next, end, &prev);
      if(m.next <= limit && !visit(m.kind, m.next, glyphs)) return;
      i = m.next;
    }
  }

  /** Where a sentence longer than a page ends its first page: about an even share of it. */
  int splitSentence(const char* t, int off, int fitEnd, int end, P64::Loc::PageFitFn fit, void* user, bool byChar)
  {
    int sentenceEnd = end;
    walk(t, off, end, end, byChar, [&](Brk kind, int pos, int) {
      if(kind != Brk::Sentence) return true;
      sentenceEnd = pos;
      return false;
    });

    // the pages it takes when each is filled
    int pages = 0;
    for(int o = off; o < sentenceEnd && pages < MAX_SENTENCE_PAGES && !restIsBlank(t, o, sentenceEnd);) {
      const int f = fit(t, o, sentenceEnd, user);
      const int e = (f <= 0 || o + f >= sentenceEnd) ? sentenceEnd : o + f;
      ++pages;
      o = skipBlank(t, e, sentenceEnd);
    }
    if(pages < 2) pages = 2;

    const int total = countGlyphs(t, off, sentenceEnd, end);
    const int room = countGlyphs(t, off, fitEnd, end);  // what a page holds, about
    const int restMax = (pages - 1) * room;
    // the distance to the even point, in 1/pages of a letter: |letters * pages - total|
    auto distance = [&](int g) { const int d = g * pages - total; return d < 0 ? -d : d; };

    int soft = -1, softDist = 0, word = -1, wordDist = 0, last = -1;
    walk(t, off, fitEnd, end, byChar, [&](Brk kind, int pos, int g) {
      if(g <= 0) return true;
      last = pos;
      if(total - g > restMax) return true; // so early the rest would need one more page
      const int d = distance(g);
      if(kind == Brk::Word) {
        if(word < 0 || d < wordDist) { word = pos; wordDist = d; }
      } else if(d * 10 <= total * SOFT_REACH_TENTHS && (soft < 0 || d < softDist)) {
        soft = pos;
        softDist = d;
      }
      return true;
    });
    if(soft >= 0) return soft;
    if(word >= 0) return word;
    if(last >= 0) return last;
    return fitEnd; // one word longer than the page: where the layout cut it
  }

  /** The end of the page that starts at 'off'. */
  int pageEnd(const char* t, int off, int end, P64::Loc::PageFitFn fit, void* user, bool byChar)
  {
    const int f = fit(t, off, end, user);
    const int fitEnd = (f <= 0 || off + f >= end) ? end : off + f;
    if(fitEnd >= end || restIsBlank(t, fitEnd, end)) return end;

    int lastSentence = -1;
    walk(t, off, fitEnd, end, byChar, [&](Brk kind, int pos, int) {
      if(kind == Brk::Sentence) lastSentence = pos;
      return true;
    });
    if(lastSentence > off) return lastSentence;
    return splitSentence(t, off, fitEnd, end, fit, user, byChar);
  }
}

int P64::Loc::splitPages(const char* text, int start, int end, PageFitFn fit, void* user, bool byChar,
                         PageSpan* out, int maxPages)
{
  if(maxPages <= 0) return 0;
  int count = 0;
  int off = start;
  while(count < maxPages) {
    if(off >= end || (count > 0 && restIsBlank(text, off, end))) {
      if(count == 0) out[count++] = {start, 0};
      break;
    }
    const int b = pageEnd(text, off, end, fit, user, byChar);
    out[count++] = {off, b - off};
    off = skipBlank(text, b, end);
  }
  return count;
}
