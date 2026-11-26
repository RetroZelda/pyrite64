/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, sections 5 and 6).
#include "locCompile.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <charconv>
#include <cmath>
#include <format>
#include <regex>

namespace
{
  using namespace P64::Loc;
  constexpr size_t SIZES_MAX_COUNT = 4; // engine text/locFormat.h SIZES_MAX
  using Project::Loc::CompileEnv;

  void putCp(std::string &out, uint32_t cp)
  {
    char buf[4];
    const int n = utf8Encode(cp, buf, 4);
    out.append(buf, static_cast<size_t>(n));
  }

  void putToken(std::string &out, Tok op, uint16_t arg) { putCp(out, token(op, arg)); }

  void putHash(std::string &out, uint32_t hash)
  {
    putToken(out, Tok::Data, uint16_t(hash >> 24));
    putToken(out, Tok::Data, uint16_t((hash >> 12) & 0xFFF));
    putToken(out, Tok::Data, uint16_t(hash & 0xFFF));
  }

  std::string trim(const std::string &s)
  {
    size_t a = s.find_first_not_of(" \t");
    if(a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
  }

  std::string lower(std::string s)
  {
    for(auto &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
  }

  bool parseHexColor(const std::string &s, uint32_t &rgba)
  {
    if(s.size() != 7 && s.size() != 9) return false;
    if(s[0] != '#') return false;
    uint32_t v = 0;
    for(size_t i = 1; i < s.size(); ++i) {
      const char c = s[i];
      v <<= 4;
      if(c >= '0' && c <= '9') v |= uint32_t(c - '0');
      else if(c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
      else if(c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
      else return false;
    }
    rgba = s.size() == 7 ? (v << 8) | 0xFF : v;
    return true;
  }

  bool parseInt(const std::string &s, int &out)
  {
    const char* b = s.data();
    const char* e = s.data() + s.size();
    if(b != e && *b == '+') ++b;
    auto res = std::from_chars(b, e, out);
    return res.ec == std::errc{} && res.ptr == e;
  }

  bool parseFloat(const std::string &s, float &out)
  {
    try {
      size_t used = 0;
      out = std::stof(s, &used);
      return used == s.size();
    } catch(...) {
      return false;
    }
  }

  /** The position of the '}' closing the '{' at 'open', counting nested braces; npos if none. */
  size_t matchBrace(const std::string &s, size_t open)
  {
    int depth = 0;
    for(size_t i = open; i < s.size(); ++i) {
      if(s[i] == '{') ++depth;
      else if(s[i] == '}' && --depth == 0) return i;
    }
    return std::string::npos;
  }

  struct Compiler
  {
    CompileEnv &env;
    PluralRule rule;
    std::vector<std::string> &errors;
    std::vector<std::pair<std::string, Group>> open{}; // tags left open, for closing checks

    void error(const std::string &msg) { errors.push_back(msg); }

    void compile(const std::string &src, std::string &out, bool inPlural)
    {
      for(size_t i = 0; i < src.size();) {
        const char c = src[i];
        const char n = i + 1 < src.size() ? src[i + 1] : '\0';

        if(c == '\\' && n == 'n') { out += '\n'; i += 2; continue; }
        if(c == '\\' && n == '\\') { out += '\\'; i += 2; continue; }
        if(c == '[' && n == '[') { out += '['; i += 2; continue; }
        if(c == '{' && n == '{') { out += '{'; i += 2; continue; }
        if(c == '$' || c == '^') { out += c; out += c; ++i; continue; }
        if(c == '#' && inPlural) { putToken(out, Tok::Number, 0); ++i; continue; }
        if(c == '\r' || (uint8_t(c) < 0x20 && c != '\n' && c != '\t')) { ++i; continue; }

        if(c == '[') {
          const size_t close = src.find(']', i);
          if(close == std::string::npos) {
            error("a [ has no ] (write [[ for a literal [)");
            out += src.substr(i);
            return;
          }
          tag(trim(src.substr(i + 1, close - i - 1)), out);
          i = close + 1;
          continue;
        }

        if(c == '{') {
          const size_t close = matchBrace(src, i);
          if(close == std::string::npos) {
            error("a { has no } (write {{ for a literal {)");
            out += src.substr(i);
            return;
          }
          variable(src.substr(i + 1, close - i - 1), out);
          i = close + 1;
          continue;
        }

        // a code point is copied as is
        const char* p = src.data() + i;
        const char* start = p;
        const uint32_t cp = utf8Decode(p);
        if(isControl(cp) || isToken(cp)) {
          error(std::format("the character U+{:X} is reserved for the formatter", cp));
        } else {
          out.append(start, static_cast<size_t>(p - start));
        }
        i += static_cast<size_t>(p - start);
      }
    }

    void openTag(const std::string &name, Group g) { open.emplace_back(name, g); }

    void closeTag(const std::string &name, std::string &out)
    {
      for(size_t k = open.size(); k-- > 0;) {
        if(open[k].first == name) {
          putToken(out, Tok::Pop, uint16_t(open[k].second));
          open.erase(open.begin() + static_cast<std::ptrdiff_t>(k));
          return;
        }
      }
      error("[/" + name + "] closes nothing");
    }

    void tag(const std::string &content, std::string &out)
    {
      if(content.empty()) { error("an empty tag []"); return; }
      if(content[0] == '/') {
        const std::string name = lower(trim(content.substr(1)));
        if(name == "b" || name == "i" || name == "size" || name == "font" || name == "color") { closeTag(name, out); return; }
        if(std::find(env.effects.begin() + 1, env.effects.end(), name) != env.effects.end()) { closeTag(name, out); return; }
        error("[/" + name + "] is not a tag that closes");
        return;
      }
      const size_t eq = content.find('=');
      const std::string name = lower(trim(content.substr(0, eq)));
      const bool hasValue = eq != std::string::npos;
      const std::string value = hasValue ? trim(content.substr(eq + 1)) : std::string{};

      auto noValue = [&]() { if(hasValue) error("[" + name + "] takes no value"); };
      auto needValue = [&]() { if(!hasValue || value.empty()) { error("[" + name + "=...] needs a value"); return false; } return true; };

      if(name == "b") { noValue(); putToken(out, Tok::Bold, 0); openTag(name, Group::Bold); return; }
      if(name == "i") { noValue(); putToken(out, Tok::Italic, 0); openTag(name, Group::Italic); return; }
      if(name == "page") { noValue(); putCp(out, control(CTRL_BUILTIN, CTRL_PAGE)); return; }

      if(name == "size") {
        if(!needValue()) return;
        int step = 0;
        const std::string v = lower(value);
        if(v == "big" || v == "bigger" || v == "large") step = 1;
        else if(v == "small" || v == "smaller") step = -1;
        else if(v == "normal") step = 0;
        else if(!parseInt(v, step) || step < -8 || step > 8) { error("[size=" + value + "]: use big, small, normal, or a step like +1 or -2"); return; }
        putToken(out, Tok::Size, uint16_t(SIZE_ZERO + step));
        openTag(name, Group::Size);
        return;
      }
      if(name == "font") {
        if(!needValue()) return;
        const int idx = env.fontSetIndex(value);
        if(idx < 0) { error("[font=" + value + "]: no font set has this name in locale.json"); return; }
        putToken(out, Tok::Font, uint16_t(idx));
        openTag(name, Group::Font);
        return;
      }
      if(name == "color" || name == "colour") {
        if(!needValue()) return;
        int idx = env.paletteIndex(lower(value));
        if(idx < 0) {
          uint32_t rgba = 0;
          if(!parseHexColor(value, rgba)) { error("[color=" + value + "]: not a color of locale.json, nor #rrggbb"); return; }
          if(env.palette.size() >= 31) { error("[color=" + value + "]: the palette is full (31 colors)"); return; }
          env.palette.push_back(lower(value));
          env.paletteRgba.push_back(rgba);
          idx = static_cast<int>(env.palette.size()) - 1;
        }
        putToken(out, Tok::Color, uint16_t(idx));
        openTag("color", Group::Color);
        return;
      }
      if(name == "btn" || name == "button") {
        if(!needValue()) return;
        auto it = std::find(env.icons.begin(), env.icons.end(), lower(value));
        if(it == env.icons.end()) { error("[btn=" + value + "]: no icon has this name in locale.json"); return; }
        putToken(out, Tok::Icon, uint16_t(it - env.icons.begin()));
        return;
      }

      auto eff = std::find(env.effects.begin() + 1, env.effects.end(), name);
      if(eff != env.effects.end()) {
        noValue();
        putToken(out, Tok::Effect, uint16_t(eff - env.effects.begin()));
        openTag(name, Group::Effect);
        return;
      }

      for(size_t op = 0; op < env.controls.size(); ++op) {
        auto &ctl = env.controls[op];
        if(ctl.name != name) continue;
        uint16_t arg = 0;
        if(ctl.kind == "none") {
          noValue();
        } else if(ctl.kind == "seconds") {
          float sec = 0;
          if(!needValue()) return;
          if(!parseFloat(value, sec) || sec < 0 || sec > 40.95f) { error("[" + name + "=" + value + "]: seconds from 0 to 40.95"); return; }
          arg = uint16_t(std::lround(sec * 100.0f));
        } else if(ctl.kind == "number") {
          int v = 0;
          if(!needValue()) return;
          if(!parseInt(value, v) || v < 0 || v > int(CODE_ARG_MAX)) { error("[" + name + "=" + value + "]: a whole number from 0 to 4093"); return; }
          arg = uint16_t(v);
        } else { // name
          if(!needValue()) return;
          auto it = std::find(ctl.names.begin(), ctl.names.end(), value);
          if(it == ctl.names.end()) {
            if(ctl.names.size() > CODE_ARG_MAX) { error("[" + name + "]: too many different values"); return; }
            ctl.names.push_back(value);
            it = ctl.names.end() - 1;
          }
          arg = uint16_t(it - ctl.names.begin());
        }
        putCp(out, control(uint8_t(op + 1), arg));
        return;
      }

      error("[" + content + "] is not a tag (see the tags of locale.json)");
    }

    void variable(const std::string &content, std::string &out)
    {
      // {name}, {name:fmt}, {name, plural, cases}
      const size_t comma = content.find(',');
      std::string head = trim(content.substr(0, comma));
      std::string fmt{};
      const size_t colon = head.find(':');
      if(colon != std::string::npos) {
        fmt = trim(head.substr(colon + 1));
        head = trim(head.substr(0, colon));
      }
      const std::string &name = head;
      auto it = env.vars.find(name);
      if(it == env.vars.end()) {
        error("{" + name + "} is not a variable declared in C++ (P64_LOC_VAR or P64_LOC_ARG)");
        return;
      }
      const uint32_t hash = varHash(name.data(), name.size());

      if(comma == std::string::npos) {
        uint16_t flags = 0;
        if(!fmt.empty()) {
          if(it->second.type != "Int") { error("{" + name + ":" + fmt + "}: only Int variables take a format"); return; }
          int pad = 0;
          if(fmt == "n") flags |= VAR_NO_GROUPING;
          else if(fmt.size() >= 2 && fmt[0] == '0' && parseInt(fmt.substr(1), pad) && pad >= 1 && pad <= 15) flags |= uint16_t(pad << VAR_PAD_SHIFT);
          else { error("{" + name + ":" + fmt + "}: use :n (no digit grouping) or :0N (zero padded to N digits)"); return; }
        }
        putToken(out, Tok::Var, flags);
        putHash(out, hash);
        return;
      }

      // plural
      const std::string rest = content.substr(comma + 1);
      const size_t comma2 = rest.find(',');
      const std::string kind = lower(trim(rest.substr(0, comma2)));
      if(kind != "plural" || comma2 == std::string::npos) {
        error("{" + name + ", " + kind + ", ...}: only plural is supported");
        return;
      }
      if(it->second.type != "Int") { error("{" + name + ", plural, ...}: the variable must be an Int"); return; }

      putToken(out, Tok::Plural, 0);
      putHash(out, hash);
      const std::string cases = rest.substr(comma2 + 1);
      bool hasOther = false;
      size_t i = 0;
      while(i < cases.size()) {
        while(i < cases.size() && (cases[i] == ' ' || cases[i] == '\t' || cases[i] == '\n')) ++i;
        if(i >= cases.size()) break;
        const size_t brace = cases.find('{', i);
        if(brace == std::string::npos) { error("{" + name + ", plural}: a case has no {text}"); break; }
        const std::string sel = lower(trim(cases.substr(i, brace - i)));
        const size_t close = matchBrace(cases, brace);
        if(close == std::string::npos) { error("{" + name + ", plural}: a case's { has no }"); break; }

        uint16_t arg = 0;
        int exact = 0;
        if(sel.size() > 1 && sel[0] == '=' && parseInt(sel.substr(1), exact) && exact >= 0 && exact <= int(CODE_ARG_MAX - CASE_EXACT)) {
          arg = uint16_t(CASE_EXACT + exact);
        } else if(sel == "zero") arg = uint16_t(PluralCat::Zero);
        else if(sel == "one") arg = uint16_t(PluralCat::One);
        else if(sel == "two") arg = uint16_t(PluralCat::Two);
        else if(sel == "few") arg = uint16_t(PluralCat::Few);
        else if(sel == "many") arg = uint16_t(PluralCat::Many);
        else if(sel == "other") { arg = uint16_t(PluralCat::Other); hasOther = true; }
        else { error("{" + name + ", plural}: '" + sel + "' is not a case (zero, one, two, few, many, other, or =N)"); }

        putToken(out, Tok::Case, arg);
        compile(cases.substr(brace + 1, close - brace - 1), out, true);
        i = close + 1;
      }
      if(!hasOther) error("{" + name + ", plural}: the 'other' case is required");
      putToken(out, Tok::End, 0);
      (void)rule;
    }
  };
}

int Project::Loc::CompileEnv::fontSetIndex(const std::string &name) const
{
  for(size_t i = 0; i < fontSets.size(); ++i) if(fontSets[i].name == name) return static_cast<int>(i);
  return -1;
}

int Project::Loc::CompileEnv::paletteIndex(const std::string &name) const
{
  for(size_t i = 0; i < palette.size(); ++i) if(palette[i] == name) return static_cast<int>(i);
  return -1;
}

Project::Loc::CompileEnv Project::Loc::makeCompileEnv(const Config &config, const VarDecls &vars, std::vector<std::string> &errors)
{
  CompileEnv env{};
  env.vars = vars;
  const auto &raw = config.raw;

  // colors: "default" first (white unless given), then the others by name
  env.palette.push_back("default");
  env.paletteRgba.push_back(0xFFFFFFFFu);
  if(raw.contains("colors") && raw["colors"].is_object()) {
    for(const auto &[name, val] : raw["colors"].items()) {
      uint32_t rgba = 0;
      if(!val.is_string() || !parseHexColor(val.get<std::string>(), rgba)) {
        errors.push_back("locale.json: colors: '" + name + "' is not #rrggbb or #rrggbbaa");
        continue;
      }
      if(lower(name) == "default") { env.paletteRgba[0] = rgba; continue; }
      if(env.palette.size() >= 30) { errors.push_back("locale.json: colors: at most 29 named colors"); break; }
      env.palette.push_back(lower(name));
      env.paletteRgba.push_back(rgba);
    }
  }

  env.effects.push_back("");
  if(raw.contains("effects") && raw["effects"].is_array()) {
    for(const auto &e : raw["effects"]) {
      if(!e.is_string()) continue;
      if(env.effects.size() >= 8) { errors.push_back("locale.json: effects: at most 7"); break; }
      env.effects.push_back(lower(e.get<std::string>()));
    }
  }

  static const std::set<std::string> RESERVED{"b", "i", "size", "font", "color", "colour", "btn", "button", "page"};
  if(raw.contains("controls") && raw["controls"].is_object()) {
    for(const auto &[name, kind] : raw["controls"].items()) {
      ControlDef ctl{.name = lower(name), .kind = kind.is_string() ? kind.get<std::string>() : std::string{"none"}};
      if(ctl.kind != "seconds" && ctl.kind != "number" && ctl.kind != "name" && ctl.kind != "none") {
        errors.push_back("locale.json: controls: '" + name + "' must be seconds, number, name or none");
        continue;
      }
      if(RESERVED.contains(ctl.name)) { errors.push_back("locale.json: controls: '" + name + "' is a built-in tag"); continue; }
      if(env.controls.size() >= CONTROLS_MAX) { errors.push_back("locale.json: controls: at most 14"); break; }
      env.controls.push_back(ctl);
    }
  }

  if(raw.contains("fontSets") && raw["fontSets"].is_object()) {
    static const char* VARIANT_KEYS[4] = {"regular", "bold", "italic", "boldItalic"};
    for(const auto &[name, def] : raw["fontSets"].items()) {
      if(!def.is_object()) continue;
      FontSetDef fs{.name = name};
      for(int v = 0; v < 4; ++v) {
        if(!def.contains(VARIANT_KEYS[v])) continue;
        const auto &entry = def[VARIANT_KEYS[v]];
        if(entry.is_string()) {
          fs.files[v] = entry.get<std::string>();
        } else if(entry.is_object()) {
          fs.files[v] = entry.value("file", std::string{});
          if(entry.contains("varAxis") && entry["varAxis"].is_array()) {
            for(const auto &a : entry["varAxis"]) if(a.is_string()) fs.varAxis[v].push_back(a);
          }
        }
      }
      if(fs.files[0].empty()) errors.push_back("locale.json: fontSets: '" + name + "' has no regular font");
      if(def.contains("sizes") && def["sizes"].is_array()) {
        fs.sizes.clear();
        for(const auto &s : def["sizes"]) if(s.is_number_integer()) fs.sizes.push_back(s.get<int>());
        if(fs.sizes.empty()) fs.sizes.push_back(16);
        if(fs.sizes.size() > SIZES_MAX_COUNT) {
          errors.push_back("locale.json: fontSets: '" + name + "' has more than 4 sizes");
          fs.sizes.resize(SIZES_MAX_COUNT);
        }
      }
      const int base = def.value("base", fs.sizes[0]);
      auto bi = std::find(fs.sizes.begin(), fs.sizes.end(), base);
      fs.base = bi == fs.sizes.end() ? 0 : static_cast<int>(bi - fs.sizes.begin());
      fs.outline = def.value("outline", 0.0f);
      fs.monochrome = def.value("monochrome", false);
      if(def.contains("outlineColor") && def["outlineColor"].is_string()) {
        uint32_t rgba = 0;
        if(parseHexColor(def["outlineColor"].get<std::string>(), rgba)) fs.outlineColor = rgba;
      }
      env.fontSets.push_back(fs);
    }
  }

  if(raw.contains("icons") && raw["icons"].is_object()) {
    const auto &icons = raw["icons"];
    env.iconSet = icons.value("set", std::string{});
    env.iconBase = icons.value("base", 0xE000u);
    if(icons.contains("names") && icons["names"].is_array()) {
      for(const auto &n : icons["names"]) if(n.is_string()) env.icons.push_back(lower(n.get<std::string>()));
    }
    if(!env.iconSet.empty() && env.fontSetIndex(env.iconSet) < 0) {
      errors.push_back("locale.json: icons: the font set '" + env.iconSet + "' does not exist");
    }
  }

  for(const auto &lang : config.languages) {
    if(!lang.pseudo && env.fontSetIndex(lang.fonts) < 0 && !env.fontSets.empty()) {
      errors.push_back("locale.json: language '" + lang.code + "' uses the font set '" + lang.fonts + "', which does not exist");
    }
  }
  return env;
}

P64::Loc::PluralRule Project::Loc::pluralRuleOf(const Language &lang)
{
  const std::string key = lower(lang.plural.empty() ? lang.code : lang.plural);
  auto is = [&](std::initializer_list<const char*> list) {
    for(auto *c : list) if(key == c || key.starts_with(std::string(c) + "-") || key.starts_with(std::string(c) + "_")) return true;
    return false;
  };
  if(key == "other" || is({"ja", "zh", "ko", "th", "vi", "id"})) return PluralRule::OtherOnly;
  if(key == "spanish" || is({"es", "it", "ca"})) return PluralRule::Spanish;
  if(key == "french" || is({"fr", "pt"})) return PluralRule::French;
  return PluralRule::OneOther;
}

std::pair<char, uint8_t> Project::Loc::groupingOf(const Language &lang)
{
  const std::string code = lower(lang.code);
  char sep = ',';
  uint8_t min = 4;
  if(code.starts_with("es")) { sep = '.'; min = 5; }
  else if(code.starts_with("pt") || code.starts_with("de") || code.starts_with("it") || code.starts_with("nl")) sep = '.';
  else if(code.starts_with("fr")) sep = ' ';
  return {sep, min};
}

std::string Project::Loc::compileText(const std::string &source, CompileEnv &env, P64::Loc::PluralRule rule,
                                      std::vector<std::string> &errors)
{
  std::string out{};
  Compiler c{env, rule, errors};
  c.compile(source, out, false);
  return out;
}

void Project::Loc::collectGlyphs(const std::string &compiled, std::set<uint32_t> &out)
{
  const char* p = compiled.c_str();
  const char* end = p + compiled.size();
  while(p < end && *p) {
    if((p[0] == '$' || p[0] == '^') && p[1] == p[0]) { out.insert(uint32_t(uint8_t(p[0]))); p += 2; continue; }
    const uint32_t cp = utf8Decode(p);
    if(cp == '\n' || cp == '\t' || isControl(cp) || isToken(cp)) continue;
    out.insert(cp);
  }
}

bool Project::Loc::usesOtherFontSets(const std::string &compiled)
{
  const char* p = compiled.c_str();
  while(*p) {
    const uint32_t cp = utf8Decode(p);
    if(isToken(cp) && static_cast<Tok>(codeOp(cp)) == Tok::Font) return true;
  }
  return false;
}

void Project::Loc::scanVarDecls(const std::string &code, const std::string &file, VarDecls &out, std::vector<std::string> &errors)
{
  static const std::regex re{R"(\bP64_LOC_(VAR|ARG)\s*\(\s*(\w+)\s*,\s*(\w+)\s*\))"};
  const std::string blank = blankComments(code);
  for(auto it = std::sregex_iterator(blank.begin(), blank.end(), re); it != std::sregex_iterator(); ++it) {
    const auto pos = static_cast<size_t>(it->position(0));
    // skip the macro's own #define
    const size_t lineStart = blank.rfind('\n', pos);
    const std::string linePrefix = blank.substr(lineStart == std::string::npos ? 0 : lineStart + 1, pos - (lineStart == std::string::npos ? 0 : lineStart + 1));
    if(linePrefix.find("#define") != std::string::npos) continue;

    const int line = 1 + static_cast<int>(std::count(blank.begin(), blank.begin() + static_cast<std::ptrdiff_t>(pos), '\n'));
    const std::string name = (*it)[2].str();
    const std::string type = (*it)[3].str();
    if(type != "Int" && type != "Key" && type != "Text") {
      errors.push_back(std::format("{}:{}: P64_LOC_{}({}, {}): the type is Int, Key or Text", file, line, (*it)[1].str(), name, type));
      continue;
    }
    auto prev = out.find(name);
    if(prev != out.end()) {
      errors.push_back(std::format("{}:{}: the variable '{}' is declared again (first at {}:{})", file, line, name, prev->second.file, prev->second.line));
      continue;
    }
    out[name] = {.type = type, .isArg = (*it)[1].str() == "ARG", .file = file, .line = line};
  }
}
