/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, section 4).
#include "localization.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <regex>
#include <unordered_map>

#include "../../../n64/engine/include/text/locId.h"
#include "../../../n64/engine/include/text/locTable.h"

namespace
{
  void put32(std::vector<uint8_t> &out, uint32_t v)
  {
    out.push_back(uint8_t(v >> 24));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
  }

  void put16(std::vector<uint8_t> &out, uint16_t v)
  {
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
  }

  std::string hex32(uint32_t v) { return std::format("{:08x}", v); }

  bool parseHex32(const std::string &s, uint32_t &out)
  {
    if(s.empty() || s.size() > 8) return false;
    uint32_t v = 0;
    for(char c : s) {
      v <<= 4;
      if(c >= '0' && c <= '9') v |= uint32_t(c - '0');
      else if(c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
      else if(c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
      else return false;
    }
    out = v;
    return true;
  }

  // Pseudo-language look-alikes for a-z and A-Z (all in Latin-1 Supplement and Latin Extended-A/B).
  const char* const PSEUDO_LOWER[26] = {
    "à","ƀ","ç","ď","é","ƒ","ĝ","ĥ","î","ĵ","ķ","ļ","ṁ","ñ","ö","þ","ǫ","ŕ","š","ţ","û","ṽ","ŵ","ẋ","ý","ž"
  };
  const char* const PSEUDO_UPPER[26] = {
    "Å","Ɓ","Ç","Ď","É","Ƒ","Ĝ","Ĥ","Î","Ĵ","Ķ","Ļ","Ṁ","Ñ","Ö","Þ","Ǫ","Ŕ","Š","Ţ","Û","Ṽ","Ŵ","Ẋ","Ý","Ž"
  };
}

// ---------------------------------------------------------------- config

int Project::Loc::Config::indexOf(const std::string &code) const
{
  for(size_t i = 0; i < languages.size(); ++i) {
    if(languages[i].code == code) return static_cast<int>(i);
  }
  return -1;
}

const Project::Loc::Language* Project::Loc::Config::find(const std::string &code) const
{
  int idx = indexOf(code);
  return idx < 0 ? nullptr : &languages[idx];
}

std::string Project::Loc::Config::pairOf(const std::string &code) const
{
  auto it = pairs.find(code);
  return it == pairs.end() ? std::string{} : it->second;
}

bool Project::Loc::Config::keeps(const std::string &key) const
{
  for(const auto &rule : keep) {
    if(rule.size() >= 2 && rule.ends_with(".*")) {
      if(key.starts_with(rule.substr(0, rule.size() - 1))) return true;
    } else if(rule == "*") {
      return true;
    } else if(rule == key) {
      return true;
    }
  }
  return false;
}

bool Project::Loc::parseConfig(const nlohmann::json &doc, Config &out, std::vector<std::string> &errors)
{
  out = {};
  if(!doc.is_object()) {
    errors.push_back("locale.json: the file is not a JSON object");
    return false;
  }
  out.raw = doc;
  if(doc.contains("source") && doc["source"].is_string()) out.source = doc["source"];

  if(doc.contains("languages") && doc["languages"].is_array()) {
    for(const auto &l : doc["languages"]) {
      if(!l.is_object() || !l.contains("code") || !l["code"].is_string()) {
        errors.push_back("locale.json: a language has no \"code\"");
        continue;
      }
      Language lang{};
      lang.code = l["code"];
      lang.name = l.value("name", lang.code);
      lang.fonts = l.value("fonts", std::string{"latin"});
      lang.wrap = l.value("wrap", std::string{"word"});
      lang.plural = l.value("plural", std::string{});
      lang.revealScale = l.value("revealScale", 1.0f);
      lang.pseudo = l.value("pseudo", false);
      if(lang.wrap != "word" && lang.wrap != "char") {
        errors.push_back("locale.json: language '" + lang.code + "': wrap must be \"word\" or \"char\"");
        lang.wrap = "word";
      }
      if(out.indexOf(lang.code) >= 0) {
        errors.push_back("locale.json: language '" + lang.code + "' is listed twice");
        continue;
      }
      out.languages.push_back(lang);
    }
  }

  if(doc.contains("pairs") && doc["pairs"].is_object()) {
    for(const auto &[code, pair] : doc["pairs"].items()) {
      if(pair.is_string()) out.pairs[code] = pair;
    }
  }
  if(doc.contains("keep") && doc["keep"].is_array()) {
    for(const auto &k : doc["keep"]) {
      if(k.is_string()) out.keep.push_back(k);
    }
  }

  // the source language always comes first, so its index is 0 at runtime
  int src = out.indexOf(out.source);
  if(src < 0) {
    errors.push_back("locale.json: the source language '" + out.source + "' is not in the language list");
    if(out.languages.empty()) out.languages.push_back({.code = out.source, .name = out.source});
    else out.source = out.languages.front().code;
  } else if(src > 0) {
    std::rotate(out.languages.begin(), out.languages.begin() + src, out.languages.begin() + src + 1);
  }
  if(out.find(out.source)->pseudo) errors.push_back("locale.json: the source language cannot be a pseudo-language");

  for(const auto &[code, pair] : out.pairs) {
    if(out.indexOf(code) < 0) errors.push_back("locale.json: pairs: '" + code + "' is not a language of the list");
    if(out.indexOf(pair) < 0) errors.push_back("locale.json: pairs: '" + pair + "' is not a language of the list");
  }
  return true;
}

std::string Project::Loc::serializeConfig(const Config &config)
{
  nlohmann::json doc = config.raw.is_object() ? config.raw : nlohmann::json::object();
  doc["source"] = config.source;
  auto langs = nlohmann::json::array();
  for(const auto &l : config.languages) {
    nlohmann::json j{};
    j["code"] = l.code;
    j["name"] = l.name;
    j["fonts"] = l.fonts;
    j["wrap"] = l.wrap;
    if(!l.plural.empty()) j["plural"] = l.plural;
    if(l.revealScale != 1.0f) j["revealScale"] = l.revealScale;
    if(l.pseudo) j["pseudo"] = true;
    langs.push_back(j);
  }
  doc["languages"] = langs;
  auto pairs = nlohmann::json::object();
  for(const auto &[code, pair] : config.pairs) pairs[code] = pair;
  doc["pairs"] = pairs;
  auto keep = nlohmann::json::array();
  for(const auto &k : config.keep) keep.push_back(k);
  doc["keep"] = keep;
  return doc.dump(2) + "\n";
}

// ---------------------------------------------------------------- tables

bool Project::Loc::parseTable(const nlohmann::json &doc, Table &out, std::vector<std::string> &errors)
{
  out.entries.clear();
  if(doc.is_null()) return true; // an empty file
  if(!doc.is_object()) {
    errors.push_back(out.code + ".json: the file is not a JSON object");
    return false;
  }
  for(const auto &[key, val] : doc.items()) {
    Entry e{};
    if(val.is_string()) {
      e.text = val;
    } else if(val.is_object()) {
      e.text = val.value("text", std::string{});
      e.note = val.value("note", std::string{});
      e.keep = val.value("keep", false);
      if(val.contains("from") && val["from"].is_string()) {
        if(!parseHex32(val["from"], e.from)) errors.push_back(out.code + ".json: '" + key + "': \"from\" is not a hex number");
      }
    } else {
      errors.push_back(out.code + ".json: '" + key + "' is neither a text nor an object");
      continue;
    }
    std::string why{};
    if(!isValidKey(key, &why)) errors.push_back(out.code + ".json: '" + key + "': " + why);
    out.entries[key] = e;
  }
  return true;
}

std::string Project::Loc::serializeTable(const Table &table)
{
  // nlohmann's object is a std::map: keys come out sorted, so diffs stay small
  nlohmann::json doc = nlohmann::json::object();
  for(const auto &[key, e] : table.entries) {
    nlohmann::json j = nlohmann::json::object();
    j["text"] = e.text;
    if(!e.note.empty()) j["note"] = e.note;
    if(e.from != 0) j["from"] = hex32(e.from);
    if(e.keep) j["keep"] = true;
    doc[key] = j;
  }
  return doc.dump(2) + "\n";
}

uint32_t Project::Loc::sourceHash(const std::string &text)
{
  return P64::Loc::Detail::crc32(text.data(), text.size());
}

bool Project::Loc::isValidKey(const std::string &key, std::string *why)
{
  using P64::Loc::KeyError;
  switch(P64::Loc::checkKey(key.data(), key.size())) {
    case KeyError::None: return true;
    case KeyError::Empty: if(why) *why = "the key is empty"; break;
    case KeyError::BadChar: if(why) *why = "keys use only a-z, 0-9, _ and dots"; break;
    case KeyError::EmptyPart: if(why) *why = "a key has no empty parts between dots"; break;
  }
  return false;
}

// ---------------------------------------------------------------- the project's tables

int Project::Loc::Localization::sourceIndex() const
{
  return config.indexOf(config.source);
}

const Project::Loc::Table* Project::Loc::Localization::source() const
{
  int idx = sourceIndex();
  return idx < 0 || idx >= static_cast<int>(tables.size()) ? nullptr : &tables[idx];
}

Project::Loc::Table* Project::Loc::Localization::table(const std::string &code)
{
  for(auto &t : tables) if(t.code == code) return &t;
  return nullptr;
}

const Project::Loc::Table* Project::Loc::Localization::table(const std::string &code) const
{
  for(const auto &t : tables) if(t.code == code) return &t;
  return nullptr;
}

std::set<std::string> Project::Loc::Localization::keys() const
{
  std::set<std::string> res{};
  for(const auto &t : tables) {
    for(const auto &[key, e] : t.entries) res.insert(key);
  }
  return res;
}

Project::Loc::Status Project::Loc::Localization::status(const std::string &key, size_t lang) const
{
  if(lang >= tables.size() || lang >= config.languages.size()) return Status::Ok;
  if(config.languages[lang].pseudo) return Status::Ok;
  const auto *src = source();
  const Entry *srcEntry = nullptr;
  if(src) {
    auto it = src->entries.find(key);
    if(it != src->entries.end()) srcEntry = &it->second;
  }
  if(static_cast<int>(lang) == sourceIndex()) {
    return (!srcEntry || srcEntry->text.empty()) ? Status::MissingSource : Status::Ok;
  }
  auto it = tables[lang].entries.find(key);
  if(it == tables[lang].entries.end() || it->second.text.empty()) return Status::Missing;
  if(srcEntry && it->second.from != sourceHash(srcEntry->text)) return Status::NeedsReview;
  return Status::Ok;
}

bool Project::Loc::Localization::isKept(const std::string &key) const
{
  if(config.keeps(key)) return true;
  if(const auto *src = source()) {
    auto it = src->entries.find(key);
    if(it != src->entries.end() && it->second.keep) return true;
  }
  return false;
}

std::vector<std::string> Project::Loc::Localization::addMissing(const Uses &uses)
{
  std::vector<std::string> added{};
  const auto allKeys = keys();
  for(const auto &[key, list] : uses) {
    if(!isValidKey(key)) continue;
    if(!allKeys.contains(key)) added.push_back(key);
  }
  // every stored table gets every key, so a translator sees what is missing
  std::set<std::string> wanted = allKeys;
  wanted.insert(added.begin(), added.end());
  for(size_t i = 0; i < tables.size() && i < config.languages.size(); ++i) {
    if(config.languages[i].pseudo) continue;
    for(const auto &key : wanted) {
      if(!tables[i].entries.contains(key)) tables[i].entries[key] = {};
    }
  }
  return added;
}

std::vector<std::string> Project::Loc::Localization::unused(const Uses &uses) const
{
  std::vector<std::string> res{};
  for(const auto &key : keys()) {
    if(uses.contains(key) || isKept(key)) continue;
    res.push_back(key);
  }
  return res;
}

void Project::Loc::Localization::remove(const std::vector<std::string> &list)
{
  for(auto &t : tables) {
    for(const auto &key : list) t.entries.erase(key);
  }
}

void Project::Loc::Localization::setText(const std::string &key, size_t lang, const std::string &text)
{
  if(lang >= tables.size()) return;
  auto &e = tables[lang].entries[key];
  e.text = text;
  if(static_cast<int>(lang) == sourceIndex()) return;
  e.from = 0;
  const auto *src = source();
  if(!src || text.empty()) return;
  auto it = src->entries.find(key);
  if(it != src->entries.end()) e.from = sourceHash(it->second.text);
}

void Project::Loc::Localization::confirm(const std::string &key, size_t lang)
{
  if(lang >= tables.size() || static_cast<int>(lang) == sourceIndex()) return;
  const auto *src = source();
  if(!src) return;
  auto it = src->entries.find(key);
  if(it == src->entries.end()) return;
  tables[lang].entries[key].from = sourceHash(it->second.text);
}

// ---------------------------------------------------------------- pseudo-language

std::string Project::Loc::pseudoText(const std::string &source)
{
  std::string out = "[["; // an escaped [, so the bracket is text and not a tag
  int letters = 0;
  int braceDepth = 0;
  for(size_t i = 0; i < source.size(); ++i) {
    const char c = source[i];
    // escapes [[ and {{ stay as they are
    if((c == '[' || c == '{') && i + 1 < source.size() && source[i + 1] == c) {
      out += c; out += c; ++i;
      continue;
    }
    if(c == '[' && braceDepth == 0) {
      // a tag: copied up to its ]
      size_t end = source.find(']', i);
      if(end == std::string::npos) end = source.size() - 1;
      out += source.substr(i, end - i + 1);
      i = end;
      continue;
    }
    if(c == '{') { ++braceDepth; out += c; continue; }
    if(c == '}') { if(braceDepth > 0) --braceDepth; out += c; continue; }
    // in a variable, only the text of plural forms ({n, plural, one {# coin}}) is changed
    if(braceDepth == 1) { out += c; continue; }
    if(c >= 'a' && c <= 'z') { out += PSEUDO_LOWER[c - 'a']; ++letters; continue; }
    if(c >= 'A' && c <= 'Z') { out += PSEUDO_UPPER[c - 'A']; ++letters; continue; }
    out += c;
  }
  const int pad = static_cast<int>(std::ceil(letters * 0.35f));
  if(pad > 0) {
    out += ' ';
    for(int i = 0; i < pad; ++i) out += "\xC2\xB7"; // U+00B7 middle dot
  }
  out += "]";
  return out;
}

// ---------------------------------------------------------------- C++ scan

std::string Project::Loc::blankComments(const std::string &code)
{
  std::string out = code;
  enum class St { Code, Line, Block, Str, Chr, Raw } st = St::Code;
  std::string rawEnd{};
  for(size_t i = 0; i < out.size(); ++i) {
    const char c = out[i];
    const char n = i + 1 < out.size() ? out[i + 1] : '\0';
    switch(st) {
      case St::Code:
        if(c == '/' && n == '/') { st = St::Line; out[i] = ' '; out[i + 1] = ' '; ++i; }
        else if(c == '/' && n == '*') { st = St::Block; out[i] = ' '; out[i + 1] = ' '; ++i; }
        else if(c == 'R' && n == '"' && (i == 0 || !(std::isalnum(static_cast<unsigned char>(out[i - 1])) || out[i - 1] == '_'))) {
          // raw string R"delim( ... )delim"
          size_t open = out.find('(', i + 2);
          if(open == std::string::npos) break;
          rawEnd = ")" + out.substr(i + 2, open - (i + 2)) + "\"";
          st = St::Raw;
          i = open;
        }
        else if(c == '"') st = St::Str;
        else if(c == '\'') {
          // a digit separator (1'000) is not a char literal
          if(i > 0 && std::isxdigit(static_cast<unsigned char>(out[i - 1])) && std::isxdigit(static_cast<unsigned char>(n))) break;
          st = St::Chr;
        }
        break;
      case St::Line:
        if(c == '\n') st = St::Code;
        else out[i] = ' ';
        break;
      case St::Block:
        if(c == '*' && n == '/') { out[i] = ' '; out[i + 1] = ' '; ++i; st = St::Code; }
        else if(c != '\n') out[i] = ' ';
        break;
      case St::Str:
        if(c == '\\') ++i;
        else if(c == '"' || c == '\n') st = St::Code;
        break;
      case St::Chr:
        if(c == '\\') ++i;
        else if(c == '\'' || c == '\n') st = St::Code;
        break;
      case St::Raw:
        // a raw string's text is blanked too: R"(..."a.b"_localized...)" is not a use
        if(out.compare(i, rawEnd.size(), rawEnd) == 0) { i += rawEnd.size() - 1; st = St::Code; }
        else if(c != '\n') out[i] = ' ';
        break;
    }
  }
  return out;
}

void Project::Loc::scanCpp(const std::string &code, const std::string &file, Uses &out, std::vector<std::string> &errors)
{
  static const std::regex re{R"re("([^"\\\n]*)"_localized\b)re"};
  const std::string blank = blankComments(code);
  int line = 1;
  size_t counted = 0;
  for(auto it = std::sregex_iterator(blank.begin(), blank.end(), re); it != std::sregex_iterator(); ++it) {
    const size_t pos = static_cast<size_t>(it->position(0));
    line += static_cast<int>(std::count(blank.begin() + counted, blank.begin() + pos, '\n'));
    counted = pos;
    const std::string key = (*it)[1].str();
    std::string why{};
    if(!isValidKey(key, &why)) {
      errors.push_back(std::format("{}:{}: \"{}\"_localized: {}", file, line, key, why));
      continue;
    }
    out[key].push_back({"C++", file, line});
  }
}

// ---------------------------------------------------------------- built tables

std::vector<uint8_t> Project::Loc::writeTable(std::vector<BuiltEntry> entries, uint16_t fileFlags)
{
  std::sort(entries.begin(), entries.end(), [](const BuiltEntry &a, const BuiltEntry &b) { return a.id < b.id; });

  std::string strings{};
  std::unordered_map<std::string, uint32_t> offsets{}; // the same text is stored once
  std::vector<uint32_t> entryOffsets{};
  entryOffsets.reserve(entries.size());
  for(const auto &e : entries) {
    auto it = offsets.find(e.text);
    if(it == offsets.end()) {
      it = offsets.emplace(e.text, static_cast<uint32_t>(strings.size())).first;
      strings += e.text;
      strings += '\0';
    }
    entryOffsets.push_back(it->second);
  }

  std::vector<uint8_t> out{};
  const uint32_t stringStart = P64::Loc::TABLE_HEADER_SIZE + static_cast<uint32_t>(entries.size()) * P64::Loc::TABLE_ENTRY_SIZE;
  out.push_back('P'); out.push_back('6'); out.push_back('4'); out.push_back('L');
  put16(out, P64::Loc::TABLE_VERSION);
  put16(out, fileFlags);
  put32(out, static_cast<uint32_t>(entries.size()));
  put32(out, stringStart);
  for(size_t i = 0; i < entries.size(); ++i) {
    put32(out, entries[i].id);
    put32(out, (entryOffsets[i] << 8) | entries[i].flags);
  }
  out.insert(out.end(), strings.begin(), strings.end());
  while(out.size() % 4) out.push_back(0);
  return out;
}

std::string Project::Loc::escapeForRdpq(const std::string &text)
{
  std::string out{};
  out.reserve(text.size());
  for(char c : text) {
    if(c == '$' || c == '^') out += c;
    out += c;
  }
  return out;
}
