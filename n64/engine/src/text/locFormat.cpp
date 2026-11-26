/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, sections 5.3 and 6). The formatter, free of
// libdragon so the host unit tests run it (tests/locale.cpp).
#include "text/locFormat.h"

#include <cstring>

namespace
{
  using namespace P64::Loc;

  const VarReg* varHead = nullptr;

  constexpr int GROUPS = static_cast<int>(Group::Count);
  constexpr int STACK = 8;
  constexpr int MAX_DEPTH = 3; // Key variables inside Key variables

  struct Writer
  {
    char* out;
    int size;
    int len{0};
    bool truncated{false};

    bool put(const char* s, int n)
    {
      if(truncated) return false;
      if(len + n >= size) { truncated = true; return false; }
      memcpy(out + len, s, static_cast<size_t>(n));
      len += n;
      return true;
    }
  };

  struct State
  {
    uint8_t bold{0}, italic{0};
    int8_t size{0};
    uint8_t font{0xFF}; // font set, 0xFF: the language's
    uint8_t color{0};
    uint8_t effect{0};
    int16_t saved[GROUPS][STACK]{};
    uint8_t depth[GROUPS]{};
    uint8_t emittedFont{0};
    int16_t emittedStyle{0};
    uint8_t firstFont{0};
  };

  int16_t valueOf(const State &st, Group g)
  {
    switch(g) {
      case Group::Bold: return st.bold;
      case Group::Italic: return st.italic;
      case Group::Size: return st.size;
      case Group::Font: return st.font;
      case Group::Color: return st.color;
      case Group::Effect: return st.effect;
      case Group::Count: break;
    }
    return 0;
  }

  void setValue(State &st, Group g, int16_t v)
  {
    switch(g) {
      case Group::Bold: st.bold = uint8_t(v); break;
      case Group::Italic: st.italic = uint8_t(v); break;
      case Group::Size: st.size = int8_t(v); break;
      case Group::Font: st.font = uint8_t(v); break;
      case Group::Color: st.color = uint8_t(v); break;
      case Group::Effect: st.effect = uint8_t(v); break;
      case Group::Count: break;
    }
  }

  void push(State &st, Group g, int16_t v)
  {
    const int gi = static_cast<int>(g);
    if(st.depth[gi] < STACK) st.saved[gi][st.depth[gi]] = valueOf(st, g);
    ++st.depth[gi];
    setValue(st, g, v);
  }

  void pop(State &st, Group g)
  {
    const int gi = static_cast<int>(g);
    if(gi >= GROUPS || st.depth[gi] == 0) return;
    --st.depth[gi];
    if(st.depth[gi] < STACK) setValue(st, g, st.saved[gi][st.depth[gi]]);
  }

  int clampSize(const FontSet &fs, int idx)
  {
    if(idx < 0) return 0;
    if(idx >= fs.sizeCount) return fs.sizeCount - 1;
    return idx;
  }

  uint8_t resolveFont(const State &st, const FormatEnv &env)
  {
    const uint8_t setIdx = st.font == 0xFF ? env.baseSet : st.font;
    if(!env.sets || setIdx >= env.setCount) return 0;
    const FontSet &fs = env.sets[setIdx];
    if(fs.sizeCount == 0) return 0;
    const int sz = clampSize(fs, int(fs.baseSize) + env.sizeStep + st.size);
    const int variant = (st.bold ? 1 : 0) | (st.italic ? 2 : 0);
    uint8_t id = fs.ids[variant][sz];
    if(!id && variant == 3) id = fs.ids[1][sz];   // no bold italic: bold
    if(!id) id = fs.ids[0][sz];                   // no such variant: regular
    if(!id) id = fs.ids[0][clampSize(fs, fs.baseSize)];
    return id;
  }

  char hexDigit(uint8_t v) { return char(v < 10 ? '0' + v : 'A' + (v - 10)); }

  /** Writes the font and style codes the state needs, if they differ from what rdpq has. */
  void sync(Writer &w, State &st, const FormatEnv &env)
  {
    const uint8_t font = resolveFont(st, env);
    const uint8_t style = styleId(st.color, st.effect);
    char buf[6];
    if(font && font != st.emittedFont) {
      buf[0] = '$'; buf[1] = hexDigit(font >> 4); buf[2] = hexDigit(font & 15);
      buf[3] = '^'; buf[4] = hexDigit(style >> 4); buf[5] = hexDigit(style & 15);
      if(w.put(buf, 6)) {
        if(!st.firstFont) st.firstFont = font;
        st.emittedFont = font;
        st.emittedStyle = style;
      }
    } else if(style != st.emittedStyle) {
      buf[0] = '^'; buf[1] = hexDigit(style >> 4); buf[2] = hexDigit(style & 15);
      if(w.put(buf, 3)) st.emittedStyle = style;
    }
  }

  bool isTokenAt(const char* p)
  {
    return uint8_t(p[0]) == 0xF4 && uint8_t(p[1]) >= 0x80 && uint8_t(p[1]) <= 0x8F;
  }

  uint32_t readToken(const char*& p) { return utf8Decode(p); }

  /** The hash that 3 Data tokens carry. */
  uint32_t readHash(const char*& p, const char* end)
  {
    uint32_t hash = 0;
    for(int i = 0; i < 3; ++i) {
      if(p >= end || !isTokenAt(p)) return hash;
      const uint32_t cp = readToken(p);
      hash = (hash << 12) | codeArg(cp);
    }
    return hash;
  }

  int formatNumber(char* buf, int64_t n, uint16_t flags, const FormatEnv &env)
  {
    char digits[24];
    int count = 0;
    uint64_t a = n < 0 ? uint64_t(-(n + 1)) + 1 : uint64_t(n);
    do { digits[count++] = char('0' + a % 10); a /= 10; } while(a && count < 20);
    const int pad = (flags & VAR_PAD_MASK) >> VAR_PAD_SHIFT;
    while(count < pad) digits[count++] = '0';
    const bool group = !(flags & VAR_NO_GROUPING) && pad == 0 && env.groupSep && count >= env.groupMin;

    int len = 0;
    if(n < 0) buf[len++] = '-';
    for(int i = count - 1; i >= 0; --i) {
      buf[len++] = digits[i];
      if(group && i > 0 && i % 3 == 0) buf[len++] = env.groupSep;
    }
    return len;
  }

  void putNumber(Writer &w, State &st, const FormatEnv &env, int64_t n, uint16_t flags)
  {
    char buf[40];
    const int len = formatNumber(buf, n, flags, env);
    sync(w, st, env);
    w.put(buf, len);
  }

  /** Raw text from a Text variable: '$' and '^' doubled, control characters dropped. */
  void putRaw(Writer &w, State &st, const FormatEnv &env, const char* text)
  {
    if(!text) return;
    sync(w, st, env);
    for(const char* p = text; *p && !w.truncated;) {
      const char c = *p;
      if(c == '$' || c == '^') { char two[2] = {c, c}; w.put(two, 2); ++p; continue; }
      if(uint8_t(c) < 0x20 && c != '\n') { ++p; continue; }
      const char* start = p;
      utf8Decode(p);
      w.put(start, int(p - start));
    }
  }

  void run(const char* p, const char* end, Writer &w, State &st, const FormatEnv &env, int64_t number, bool hasNumber, int depth);

  void putVar(Writer &w, State &st, const FormatEnv &env, uint32_t hash, uint16_t flags, int depth)
  {
    if(env.args) {
      if(const auto* a = env.args->find(hash)) {
        switch(a->type) {
          case VarType::Int: putNumber(w, st, env, a->v.i, flags); return;
          case VarType::Text: putRaw(w, st, env, a->v.text); return;
          case VarType::Key: {
            if(!env.keyText || depth >= MAX_DEPTH) return;
            bool fb = false;
            const char* text = env.keyText(a->v.key, &fb, env.user);
            if(text) run(text, text + strlen(text), w, st, env, 0, false, depth + 1);
            return;
          }
        }
      }
    }
    const VarReg* reg = findVar(hash);
    if(!reg) { sync(w, st, env); w.put("?", 1); return; }
    switch(reg->type) {
      case VarType::Int: putNumber(w, st, env, reg->fn.i(), flags); break;
      case VarType::Text: putRaw(w, st, env, reg->fn.t()); break;
      case VarType::Key: {
        if(!env.keyText || depth >= MAX_DEPTH) break;
        bool fb = false;
        const char* text = env.keyText(reg->fn.k().hash, &fb, env.user);
        if(text) run(text, text + strlen(text), w, st, env, 0, false, depth + 1);
        break;
      }
    }
  }

  int64_t intValue(const FormatEnv &env, uint32_t hash)
  {
    if(env.args) {
      if(const auto* a = env.args->find(hash)) return a->type == VarType::Int ? a->v.i : 0;
    }
    const VarReg* reg = findVar(hash);
    return (reg && reg->type == VarType::Int) ? reg->fn.i() : 0;
  }

  /** From after a Plural's hash: the text of the case that matches 'n'. Returns the end of the plural. */
  const char* runPlural(const char* p, const char* end, Writer &w, State &st, const FormatEnv &env, int64_t n, int depth)
  {
    const PluralCat cat = pluralCategory(env.plural, n);
    const char* exact = nullptr; const char* exactEnd = nullptr;
    const char* byCat = nullptr; const char* byCatEnd = nullptr;
    const char* other = nullptr; const char* otherEnd = nullptr;

    const char* caseStart = nullptr;
    uint16_t caseArg = 0;
    int nest = 0;
    auto close = [&](const char* at) {
      if(!caseStart) return;
      if(caseArg >= CASE_EXACT) {
        if(!exact && int64_t(caseArg - CASE_EXACT) == n) { exact = caseStart; exactEnd = at; }
      } else {
        if(!byCat && caseArg == uint16_t(cat)) { byCat = caseStart; byCatEnd = at; }
        if(caseArg == uint16_t(PluralCat::Other)) { other = caseStart; otherEnd = at; }
      }
      caseStart = nullptr;
    };

    while(p < end && *p) {
      if(!isTokenAt(p)) { utf8Decode(p); continue; }
      const char* at = p;
      const uint32_t cp = readToken(p);
      const auto op = static_cast<Tok>(codeOp(cp));
      if(op == Tok::Plural) { ++nest; continue; }
      if(op == Tok::End) {
        if(nest > 0) { --nest; continue; }
        close(at);
        break;
      }
      if(op == Tok::Case && nest == 0) {
        close(at);
        caseStart = p;
        caseArg = codeArg(cp);
      }
    }

    const char* s = exact ? exact : byCat ? byCat : other;
    const char* e = exact ? exactEnd : byCat ? byCatEnd : otherEnd;
    if(s) run(s, e, w, st, env, n, true, depth);
    return p;
  }

  void run(const char* p, const char* end, Writer &w, State &st, const FormatEnv &env, int64_t number, bool hasNumber, int depth)
  {
    while(p < end && *p && !w.truncated) {
      if(isTokenAt(p)) {
        const uint32_t cp = readToken(p);
        const uint16_t arg = codeArg(cp);
        switch(static_cast<Tok>(codeOp(cp))) {
          case Tok::Pop: pop(st, static_cast<Group>(arg)); break;
          case Tok::Bold: push(st, Group::Bold, 1); break;
          case Tok::Italic: push(st, Group::Italic, 1); break;
          case Tok::Size: push(st, Group::Size, int16_t(st.size + (int(arg) - int(SIZE_ZERO)))); break;
          case Tok::Font: push(st, Group::Font, int16_t(arg)); break;
          case Tok::Color: push(st, Group::Color, int16_t(arg)); break;
          case Tok::Effect: push(st, Group::Effect, int16_t(arg)); break;
          case Tok::Icon: {
            if(!env.sets || env.iconSet >= env.setCount) break;
            const FontSet &fs = env.sets[env.iconSet];
            if(fs.sizeCount == 0) break;
            const uint8_t id = fs.ids[0][clampSize(fs, int(fs.baseSize) + env.sizeStep + st.size)];
            if(!id) break;
            char buf[8];
            buf[0] = '$'; buf[1] = hexDigit(id >> 4); buf[2] = hexDigit(id & 15);
            const int n = utf8Encode(env.iconBase + arg, buf + 3, 4);
            if(w.put(buf, 3 + n)) { st.emittedFont = id; st.emittedStyle = 0; }
            break;
          }
          case Tok::Var: putVar(w, st, env, readHash(p, end), arg, depth); break;
          case Tok::Plural: {
            const uint32_t hash = readHash(p, end);
            p = runPlural(p, end, w, st, env, intValue(env, hash), depth);
            break;
          }
          case Tok::Number: if(hasNumber) putNumber(w, st, env, number, 0); break;
          case Tok::Case: case Tok::End: case Tok::Data: default: break;
        }
        continue;
      }

      // text: "$$" and "^^" are one unit, so a cut never leaves a lone code character
      sync(w, st, env);
      const char* start = p;
      if((p[0] == '$' || p[0] == '^') && p[1] == p[0]) p += 2;
      else utf8Decode(p);
      w.put(start, int(p - start));
    }
  }
}

P64::Loc::VarReg::VarReg(uint32_t nameHash, IntFn f) : hash{nameHash}, type{VarType::Int}, fn{}, next{varHead} { fn.i = f; varHead = this; }
P64::Loc::VarReg::VarReg(uint32_t nameHash, KeyFn f) : hash{nameHash}, type{VarType::Key}, fn{}, next{varHead} { fn.k = f; varHead = this; }
P64::Loc::VarReg::VarReg(uint32_t nameHash, TextFn f) : hash{nameHash}, type{VarType::Text}, fn{}, next{varHead} { fn.t = f; varHead = this; }

const P64::Loc::VarReg* P64::Loc::findVar(uint32_t hash)
{
  for(const VarReg* v = varHead; v; v = v->next) {
    if(v->hash == hash) return v;
  }
  return nullptr;
}

P64::Loc::Args& P64::Loc::Args::set(uint32_t hash, int32_t value)
{
  if(count < MAX) { items[count] = {hash, VarType::Int, {}}; items[count++].v.i = value; }
  return *this;
}

P64::Loc::Args& P64::Loc::Args::set(uint32_t hash, LocId value)
{
  if(count < MAX) { items[count] = {hash, VarType::Key, {}}; items[count++].v.key = value.hash; }
  return *this;
}

P64::Loc::Args& P64::Loc::Args::set(uint32_t hash, const char* value)
{
  if(count < MAX) { items[count] = {hash, VarType::Text, {}}; items[count++].v.text = value; }
  return *this;
}

const P64::Loc::Args::Item* P64::Loc::Args::find(uint32_t hash) const
{
  for(uint8_t i = 0; i < count; ++i) {
    if(items[i].hash == hash) return &items[i];
  }
  return nullptr;
}

P64::Loc::FormatResult P64::Loc::formatText(const char* compiled, bool fallback, char* out, int size, const FormatEnv &env)
{
  FormatResult res{};
  if(!out || size <= 0) return res;
  out[0] = '\0';
  if(!compiled) return res;

  Writer w{out, size};
  State st{};
  if(fallback && env.fallbackColor) st.color = env.fallbackColor;
  run(compiled, compiled + strlen(compiled), w, st, env, 0, false, 0);
  // a text that is only codes still starts with its font, so the caller can pass any font id
  if(w.len == 0) sync(w, st, env);
  out[w.len] = '\0';
  res.length = w.len;
  res.truncated = w.truncated;
  res.firstFont = st.firstFont;
  return res;
}
