/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "codeParser.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_set>

#include "fs.h"
#include "logger.h"

namespace
{
  constexpr Utils::DataType fromString(const std::string &str)
  {
    if (str == "uint8_t") return Utils::DataType::u8;
    if (str == "int8_t") return Utils::DataType::s8;
    if (str == "uint16_t") return Utils::DataType::u16;
    if (str == "int16_t") return Utils::DataType::s16;
    if (str == "uint32_t") return Utils::DataType::u32;
    if (str == "int32_t") return Utils::DataType::s32;
    if (str == "float") return Utils::DataType::f32;
    if (str == "bool") return Utils::DataType::BOOL;
    if (str == "color_t") return Utils::DataType::COLOR;
    if (str == "char") return Utils::DataType::string;
    if (str == "fm_vec3_t") return Utils::DataType::VEC3;
    if (str == "fm_quat_t") return Utils::DataType::QUAT;
    if (str == "AssetRef<sprite_t>") return Utils::DataType::ASSET_SPRITE;
    if (str == "AssetRef<emitter_t>") return Utils::DataType::ASSET_EMITTER;
    if (str == "PrefabRef") return Utils::DataType::PREFAB;
    if (str == "ObjectRef" || str == "P64::ObjectRef" || str.rfind("ObjectRef<", 0) == 0 || str.find("::ObjectRef<") != std::string::npos) return Utils::DataType::OBJECT_REF;
    return Utils::DataType::s32;
  }

  constexpr uint32_t getTypeSize(Utils::DataType type) {
    switch(type) {
      case Utils::DataType::u8:
      case Utils::DataType::s8:
      case Utils::DataType::BOOL:
        return 1;
      case Utils::DataType::u16:
      case Utils::DataType::s16:
        return 2;
      case Utils::DataType::VEC3:
        return 12;
      case Utils::DataType::QUAT:
        return 16;
      case Utils::DataType::u32:
      case Utils::DataType::s32:
      case Utils::DataType::f32:
      case Utils::DataType::COLOR:
      case Utils::DataType::ASSET_SPRITE:
      case Utils::DataType::ASSET_EMITTER:
      case Utils::DataType::OBJECT_REF:
      case Utils::DataType::PREFAB:
      default:
        return 4;
    }
  }

  // Inclusive value range an integer field of the given type can hold.
  constexpr std::pair<int64_t, int64_t> getIntRange(Utils::DataType type) {
    switch(type) {
      case Utils::DataType::u8:  return {0, UINT8_MAX};
      case Utils::DataType::s8:  return {INT8_MIN, INT8_MAX};
      case Utils::DataType::u16: return {0, UINT16_MAX};
      case Utils::DataType::s16: return {INT16_MIN, INT16_MAX};
      case Utils::DataType::u32: return {0, UINT32_MAX};
      default:                   return {INT32_MIN, INT32_MAX};
    }
  }

  std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r\"");
    size_t end = s.find_last_not_of(" \t\n\r\"");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
  }

  // Like trim(), but only removes whitespace (keeps quotes).
  std::string trimSpace(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
  }

  // Parses a whole integer literal: decimal, or hexadecimal with a 0x / 0X prefix, with an optional sign.
  // Unlike std::stoi, anything after the number ("1.5", "10abc") makes it invalid instead of being ignored.
  // A leading 0 stays decimal ("010" is 10, not octal 8).
  bool parseIntLiteral(const std::string &text, int64_t &value) {
    size_t pos = 0;
    bool negative = false;
    if (pos < text.size() && (text[pos] == '-' || text[pos] == '+')) {
      negative = text[pos] == '-';
      ++pos;
    }

    int base = 10;
    if (text.size() - pos > 2 && text[pos] == '0' && (text[pos + 1] == 'x' || text[pos + 1] == 'X')) {
      base = 16;
      pos += 2;
    }

    // from_chars takes no sign or prefix itself, so e.g. "--1" and "0x-1" are rejected
    uint64_t magnitude = 0;
    const char *end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(text.data() + pos, end, magnitude, base);
    if (ec != std::errc{} || ptr != end) return false;

    if (negative) {
      if (magnitude > static_cast<uint64_t>(INT64_MAX) + 1) return false;
      value = magnitude == 0 ? 0 : -static_cast<int64_t>(magnitude - 1) - 1; // INT64_MIN without overflow
    } else {
      if (magnitude > static_cast<uint64_t>(INT64_MAX)) return false;
      value = static_cast<int64_t>(magnitude);
    }
    return true;
  }

  // Parses a "value=name" comma separated list (e.g. "0=Fire, 1=Water") into (value, name) pairs.
  // Used for bit indices (P64::Bitmask) and enum values (P64::Enum).
  // Like in a C enum, an entry without "value=" takes the previous value + 1 (0 for the first one),
  // so "Fire, Water, Earth" is the same as "0=Fire, 1=Water, 2=Earth".
  // Values are decimal or 0x hex, an entry with anything else (e.g. "1.5=Half") is ignored with a warning.
  std::vector<std::pair<int64_t, std::string>> parseBitmask(
    const std::string& meta, const std::string &fieldName, const std::string &attrName
  ) {
    std::vector<std::pair<int64_t, std::string>> result;
    std::stringstream ss(meta);
    std::string part;
    int64_t nextValue = 0;
    while (std::getline(ss, part, ',')) {
      auto eq = part.find('=');
      std::string name = trim(eq == std::string::npos ? part : part.substr(eq + 1));
      if (name.empty()) continue;

      int64_t value = nextValue;
      if (eq != std::string::npos && !parseIntLiteral(trim(part.substr(0, eq)), value)) {
        Utils::Logger::log(
          "Script field " + fieldName + ": " + attrName + " entry '" + trimSpace(part)
            + "' is not a valid number (use decimal or 0x hex), ignored.",
          Utils::Logger::LEVEL_WARN
        );
        continue;
      }
      result.push_back({value, name});
      nextValue = value < INT64_MAX ? value + 1 : value;
    }
    return result;
  }

  // Removes (value, name) entries outside of [min, max], e.g. bit 8 of a uint8_t bitmask.
  void dropOutOfRange(
    std::vector<std::pair<int64_t, std::string>> &entries, int64_t min, int64_t max,
    const std::string &fieldName, const std::string &attrName
  ) {
    std::erase_if(entries, [&](const auto &entry) {
      if (entry.first >= min && entry.first <= max) return false;
      Utils::Logger::log(
        "Script field " + fieldName + ": " + attrName + " entry '" + std::to_string(entry.first) + "="
          + entry.second + "' is out of range (" + std::to_string(min) + " to " + std::to_string(max) + "), ignored.",
        Utils::Logger::LEVEL_WARN
      );
      return true;
    });
  }

  // If 'i' sits on the opening quote of a string or char literal, advances it past the
  // closing quote (honoring backslash escapes) and returns true. Used by the parenthesis
  // scanners so brackets inside literals (e.g. P64::Name("foo(((")) don't affect nesting.
  bool skipLiteral(const std::string& text, size_t& i) {
    char quote = text[i];
    if (quote != '"' && quote != '\'') return false;
    ++i;
    while (i < text.size() && text[i] != quote) {
      if (text[i] == '\\') ++i;
      ++i;
    }
    if (i < text.size()) ++i; // closing quote
    return true;
  }

  // Resolves an attribute value: a single string literal ("...") is returned without its quotes
  // (\" and \\ are unescaped), anything else (e.g. a number) as written, both without outer whitespace.
  std::string parseAttributeValue(const std::string& raw) {
    std::string text = trimSpace(raw);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
      std::string res{};
      size_t i = 1;
      for (; i < text.size() - 1; ++i) {
        if (text[i] == '"') break; // several literals, e.g. "a" "b"
        if (text[i] == '\\' && i + 1 < text.size() - 1) ++i;
        res += text[i];
      }
      if (i == text.size() - 1) return trimSpace(res);
    }
    return trim(text);
  }

  // Parses the content of one [[...]] block, e.g. `P64::Name("Speed"), P64::Enum("0=Off, 1=On")`,
  // into (attribute-name -> value). Commas and parentheses inside a string literal are part of the value.
  std::unordered_map<std::string, std::string> parseAttributes(const std::string& attrText) {
    std::unordered_map<std::string, std::string> result;
    std::string text = trim(attrText);
    if (text.empty()) return result;

    size_t i = 0;
    while (i < text.size()) {
      // Extract attribute name
      size_t nameStart = i;
      while (i < text.size() && text[i] != '(' && text[i] != ',' && text[i] != ']')
        ++i;
      std::string name = trim(text.substr(nameStart, i - nameStart));

      // Extract the value up to the matching ')', skipping over string literals
      std::string value;
      if (i < text.size() && text[i] == '(') {
        ++i;
        int depth = 1;
        bool inString = false;
        size_t valStart = i;
        size_t valEnd = text.size();
        for (; i < text.size(); ++i) {
          if (inString) {
            if (text[i] == '\\') ++i; // skip the escaped character
            else if (text[i] == '"') inString = false;
          } else if (text[i] == '"') {
            inString = true;
          } else if (text[i] == '(') {
            ++depth;
          } else if (text[i] == ')' && --depth == 0) {
            valEnd = i++;
            break;
          }
        }
        value = parseAttributeValue(text.substr(valStart, valEnd - valStart));
      }

      result[name] = value;

      // Skip comma and whitespace (and a stray ']' so the loop always advances)
      while (i < text.size() && (text[i] == ',' || text[i] == ']' || isspace((unsigned char)text[i])))
        ++i;
    }

    return result;
  }
}

namespace fs = std::filesystem;

namespace
{
  std::string stripComments(const std::string &sourceCode)
  {
    auto code = std::regex_replace(sourceCode, std::regex(R"(//[^\n]*)"), "");
    return std::regex_replace(code, std::regex(R"(/\*[\s\S]*?\*/)"), "");
  }

  // Returns the index after the bracket closing the one at 'start', or npos if unbalanced.
  size_t skipBlock(const std::string &text, size_t start, char open, char close)
  {
    size_t i = start + 1;
    int depth = 1;
    while (i < text.size()) {
      if (skipLiteral(text, i)) continue;
      if (text[i] == open) ++depth;
      else if (text[i] == close && --depth == 0) return i + 1;
      ++i;
    }
    return std::string::npos;
  }

  std::string removeAttributes(const std::string &text)
  {
    std::string res{};
    for (size_t i = 0; i < text.size();) {
      if (text.compare(i, 2, "[[") == 0) {
        auto end = text.find("]]", i);
        if (end == std::string::npos) break;
        i = end + 2;
        continue;
      }
      res += text[i++];
    }
    return res;
  }

  std::string firstWord(const std::string &text)
  {
    size_t end = 0;
    while (end < text.size() && (isalnum((unsigned char)text[end]) || text[end] == '_')) ++end;
    return text.substr(0, end);
  }

  bool isFunctionDecl(const std::string &stmt)
  {
    if (std::regex_search(stmt, std::regex(R"(\boperator\b)"))) return true;
    auto paren = stmt.find('(');
    if (paren == std::string::npos) return false;
    auto assign = stmt.find('=');
    return assign == std::string::npos || paren < assign;
  }

  /**
   * Reduces a struct/class body to its non-static data members, one per line.
   * Methods, nested types, access specifiers and static members are removed.
   */
  std::string extractDataMembers(const std::string &body)
  {
    static const std::unordered_set<std::string> SKIP_WORDS{
      "static", "using", "typedef", "friend", "template", "enum",
      "struct", "class", "union", "constexpr", "inline", "static_assert"
    };

    std::string res{}, stmt{};
    bool skipStmt = false;

    auto finishStmt = [&]() {
      auto plain = trimSpace(removeAttributes(stmt));
      bool isBitField = std::regex_search(plain, std::regex(R"((^|[^:]):([^:]|$))"));
      if (isBitField && !skipStmt) {
        Utils::Logger::log("Bit-fields in script data are not supported: '" + plain + "'", Utils::Logger::LEVEL_WARN);
      }
      if (!skipStmt && !isBitField && !plain.empty() && !SKIP_WORDS.contains(firstWord(plain)) && !isFunctionDecl(plain)) {
        res += trimSpace(stmt) + ";\n";
      }
      stmt.clear();
      skipStmt = false;
    };

    for (size_t i = 0; i < body.size();)
    {
      size_t litStart = i;
      if (skipLiteral(body, i)) {
        stmt += body.substr(litStart, i - litStart);
        continue;
      }

      char c = body[i];
      if (c == '{') {
        auto end = skipBlock(body, i, '{', '}');
        if (end == std::string::npos) break;
        auto plain = trimSpace(removeAttributes(stmt));
        auto word = firstWord(plain);

        if (word == "struct" || word == "class" || word == "union" || word == "enum") {
          skipStmt = true; // nested type, ignore up to the next ';'
        } else if (isFunctionDecl(plain)) {
          stmt.clear(); // method body, no ';' follows
        } else {
          stmt += body.substr(i, end - i); // brace initializer
        }
        i = end;
        continue;
      }

      if (c == ';') {
        finishStmt();
        ++i;
        continue;
      }

      bool isScope = (i + 1 < body.size() && body[i + 1] == ':') || (i > 0 && body[i - 1] == ':');
      if (c == ':' && !isScope) {
        auto plain = trimSpace(stmt);
        if (plain == "public" || plain == "private" || plain == "protected") {
          stmt.clear();
          ++i;
          continue;
        }
      }

      stmt += c;
      ++i;
    }
    return res;
  }

  std::optional<std::string> findStructBody(const std::string &code, const std::string &name)
  {
    std::regex declRegex("\\b(?:struct|class)\\s+" + name + "\\b");
    for (auto it = std::sregex_iterator(code.begin(), code.end(), declRegex); it != std::sregex_iterator(); ++it)
    {
      size_t pos = it->position() + it->length();
      while (pos < code.size() && isspace((unsigned char)code[pos])) ++pos;
      if (code.compare(pos, 5, "final") == 0) {
        pos += 5;
        while (pos < code.size() && isspace((unsigned char)code[pos])) ++pos;
      }

      if (pos < code.size() && code[pos] == ':') {
        Utils::Logger::log("Script data type '" + name + "' has base classes, which are not supported", Utils::Logger::LEVEL_WARN);
        return std::nullopt;
      }
      if (pos >= code.size() || code[pos] != '{') continue; // forward declaration or usage

      auto end = skipBlock(code, pos, '{', '}');
      if (end == std::string::npos) return std::nullopt;
      return code.substr(pos + 1, end - pos - 2);
    }
    return std::nullopt;
  }

  // Searches the struct in the code and then recursively in its quoted includes.
  std::optional<std::string> findStructInIncludes(
    const std::string &code, const std::string &name, const fs::path &dir,
    const std::vector<fs::path> &includeDirs, std::unordered_set<std::string> &visited, int depth
  ) {
    if (auto body = findStructBody(code, name)) return body;
    if (depth > 8) return std::nullopt;

    std::regex includeRegex(R"(#\s*include\s*"([^"]+)\")");
    for (auto it = std::sregex_iterator(code.begin(), code.end(), includeRegex); it != std::sregex_iterator(); ++it)
    {
      std::string inc = (*it)[1];
      std::vector<fs::path> candidates{dir / inc};
      for (auto &incDir : includeDirs) candidates.push_back(incDir / inc);

      for (auto &candidate : candidates)
      {
        std::error_code ec;
        if (!fs::is_regular_file(candidate, ec)) continue;
        auto key = fs::weakly_canonical(candidate, ec).string();
        if (!visited.insert(key).second) break;

        auto incCode = stripComments(Utils::FS::loadTextFile(candidate));
        if (auto body = findStructInIncludes(incCode, name, candidate.parent_path(), includeDirs, visited, depth + 1)) {
          return body;
        }
        break;
      }
    }
    return std::nullopt;
  }

  std::vector<Utils::CPP::Field> parseFields(const std::string &body)
  {
    using namespace Utils;
    using namespace Utils::CPP;
    std::vector<Field> fields{};
    std::string currentGroup{};
    // Regex for attributes + field lines.
    // Groups: 1 = all [[...]] blocks in front of the member, 2 = type, 3 = name, 4 = [size], 5 = default value.
    // A ']' inside a string literal of an attribute does not end its block.
    std::regex fieldRegex(
      R"(((?:\[\[(?:"(?:[^"\\\n]|\\.)*"|[^\]"])+\]\]\s*)+)?([\w:<>]+)\s+(\w+)(\[[0-9]+\])?(?:\s*\=(.*))?\s*;)"
    );
    // A single [[...]] block, group 1 = its content
    std::regex attrBlockRegex(R"(\[\[((?:"(?:[^"\\\n]|\\.)*"|[^\]"])+)\]\])");

    std::smatch fieldMatch;
    auto fieldBegin = body.cbegin();

    while (std::regex_search(fieldBegin, body.cend(), fieldMatch, fieldRegex))
    {
      // Merge the attributes of all blocks, so stacked blocks on separate lines work too,
      // e.g. [[P64::Name("Flags")]] followed by [[P64::Bitmask("...")]]. Later blocks win on duplicates.
      std::unordered_map<std::string, std::string> attr{};
      const std::string attrText = fieldMatch[1];
      for (std::sregex_iterator it{attrText.begin(), attrText.end(), attrBlockRegex}, itEnd{}; it != itEnd; ++it) {
        for (auto &[key, value] : parseAttributes((*it)[1])) {
          attr[key] = value;
        }
      }

      Field field{
        .type = fromString(fieldMatch[2]),
        .dataSize = getTypeSize(fromString(fieldMatch[2])),
        .name = fieldMatch[3],
        .attr = std::move(attr),
        .defaultValue = fieldMatch[5],
      };

      const bool isUnsigned = field.type == DataType::u8 || field.type == DataType::u16 || field.type == DataType::u32;
      const bool isSigned = field.type == DataType::s8 || field.type == DataType::s16 || field.type == DataType::s32;

      // Pre-parse the bitmask attribute for unsigned int fields, so the editor doesn't re-parse each frame.
      if (isUnsigned) {
        auto bitmaskAttr = field.attr.find("P64::Bitmask");
        if (bitmaskAttr != field.attr.end()) {
          auto bits = parseBitmask(bitmaskAttr->second, field.name, "P64::Bitmask");
          dropOutOfRange(bits, 0, field.dataSize * 8 - 1, field.name, "P64::Bitmask");
          // the remaining bit indices are 0-31
          for (auto &[bit, name] : bits) {
            field.bitmask.push_back({static_cast<int>(bit), std::move(name)});
          }
        }
      }

      // Same for the enum attribute of any integer field, it uses the same "value=Name" format.
      if (isUnsigned || isSigned) {
        auto enumAttr = field.attr.find("P64::Enum");
        if (enumAttr != field.attr.end()) {
          auto [min, max] = getIntRange(field.type);
          field.enumValues = parseBitmask(enumAttr->second, field.name, "P64::Enum");
          dropOutOfRange(field.enumValues, min, max, field.name, "P64::Enum");
        }
      }

      // Pre-parse numeric bounds: Range(min, max) gives a slider, Min(x)/Max(x) only clamp.
      if (field.isNumeric()) {
        auto rangeAttr = field.attr.find("P64::Range");
        if (rangeAttr != field.attr.end()) {
          auto values = Utils::parseFloatList(rangeAttr->second);
          if (values.size() >= 2 && values[0] <= values[1]) {
            field.min = values[0];
            field.max = values[1];
            field.slider = true;
          } else {
            Logger::log("Invalid P64::Range on field '" + field.name + "', expected (min, max)", Logger::LEVEL_WARN);
          }
        }
        auto minAttr = field.attr.find("P64::Min");
        if (minAttr != field.attr.end()) {
          auto values = Utils::parseFloatList(minAttr->second);
          if (!values.empty()) field.min = values[0];
        }
        auto maxAttr = field.attr.find("P64::Max");
        if (maxAttr != field.attr.end()) {
          auto values = Utils::parseFloatList(maxAttr->second);
          if (!values.empty()) field.max = values[0];
        }
        if (field.min && field.max && *field.min > *field.max) {
          Logger::log("Field '" + field.name + "' has min > max, ignoring bounds", Logger::LEVEL_WARN);
          field.min.reset();
          field.max.reset();
          field.slider = false;
        }
      }

      // Normalize bool defaults ("true"/"1"/empty) into "0"/"1".
      if (field.type == DataType::BOOL) {
        auto def = trim(field.defaultValue);
        field.defaultValue = (def == "true" || def == "1") ? "1" : "0";
      }

      // Normalize color defaults (e.g. "{255, 0, 0, 255}" or empty) into "r,g,b,a".
      // Macros like RGBA32(...) can't be evaluated here and fall back to opaque white.
      if (field.type == DataType::COLOR) {
        auto def = trim(field.defaultValue);
        auto values = (def.empty() || def[0] != '{') ? std::vector<float>{} : Utils::parseFloatList(def);
        values.resize(4, 255.0f);
        for (auto &v : values) v = std::clamp(v, 0.0f, 255.0f);
        field.defaultValue = Utils::floatListToString(values.data(), values.size());
      }

      // Normalize vector defaults (e.g. "{{1, 2, 3}}" or empty) into the "x,y,z" form stored by the editor.
      if (field.type == DataType::VEC3 || field.type == DataType::QUAT) {
        auto values = Utils::parseFloatList(field.defaultValue);
        if (field.type == DataType::QUAT && values.empty()) values = {0,0,0,1};
        values.resize(field.type == DataType::VEC3 ? 3 : 4, 0.0f);
        field.defaultValue = Utils::floatListToString(values.data(), values.size());
      }

      if(field.type == DataType::string) {
        try
        {
          auto strSize = fieldMatch[4].str(); // -> [42]
          field.dataSize = std::stoul(strSize.substr(1, strSize.size() - 2)); // parse without brackets
        } catch(...) {
          Logger::log(
            "Failed to parse size for string field: " + field.name + ", defaulting to 4 bytes.",
            Logger::LEVEL_ERROR
          );
          field.dataSize = 4;
        }

      }

      auto groupAttr = field.attr.find("P64::Group");
      if (groupAttr != field.attr.end()) {
        currentGroup = groupAttr->second;
      }
      field.group = currentGroup;

      fields.push_back(field);
      fieldBegin = fieldMatch.suffix().first;
    }

    return fields;
  }
}

Utils::CPP::Struct Utils::CPP::parseDataStruct(const std::string &sourceCode, const std::string &structName)
{
  auto code = stripComments(sourceCode);

  // Locate the P64_DATA(...) body by matching parentheses, so defaults that
  // contain calls or macros (e.g. "color_t c = RGBA32(...)") don't end it early.
  auto bodyStart = code.find("P64_DATA(");
  if (bodyStart == std::string::npos)return {};

  bodyStart += std::string_view{"P64_DATA("}.size();
  size_t bodyEnd = bodyStart;
  int depth = 1;
  while (bodyEnd < code.size() && depth > 0) {
    if (skipLiteral(code, bodyEnd)) continue;
    if (code[bodyEnd] == '(') ++depth;
    else if (code[bodyEnd] == ')') --depth;
    ++bodyEnd;
  }
  if (depth != 0) return {};

  Struct s{.name = "Data"};
  if (s.name != structName) return {};

  s.fields = parseFields(code.substr(bodyStart, bodyEnd - 1 - bodyStart));
  return s;
}

Utils::CPP::Struct Utils::CPP::parseScriptData(
  const std::string &sourceCode, const fs::path &sourcePath, const std::vector<fs::path> &includeDirs
) {
  auto code = stripComments(sourceCode);

  constexpr std::string_view MACRO_TYPE{"P64_DATA_TYPE("};
  auto macroPos = code.find(MACRO_TYPE);
  if (macroPos == std::string::npos) {
    return parseDataStruct(sourceCode, "Data");
  }

  auto argStart = macroPos + MACRO_TYPE.size();
  auto argEnd = code.find(')', argStart);
  if (argEnd == std::string::npos) return {};

  auto typeName = trimSpace(code.substr(argStart, argEnd - argStart));
  auto nsPos = typeName.rfind("::");
  auto name = nsPos == std::string::npos ? typeName : typeName.substr(nsPos + 2);

  Struct s{.name = "Data"};
  if (!std::regex_match(name, std::regex(R"([A-Za-z_]\w*)"))) {
    Logger::log("Invalid script data type '" + typeName + "' in " + sourcePath.filename().string(), Logger::LEVEL_WARN);
    return s;
  }

  std::unordered_set<std::string> visited{};
  auto body = findStructInIncludes(code, name, sourcePath.parent_path(), includeDirs, visited, 0);
  if (!body) {
    Logger::log("Script data type '" + typeName + "' not found in " + sourcePath.filename().string() + " or its includes", Logger::LEVEL_WARN);
    return s;
  }

  s.fields = parseFields(extractDataMembers(*body));
  return s;
}

bool Utils::CPP::hasFunction(const std::string&sourceCode, const std::string&retType, const std::string&name) {
  // remove line and multi-line comments
  auto code = std::regex_replace(sourceCode, std::regex(R"(//[^\n]*)"), "");
  code = std::regex_replace(code, std::regex(R"(/\*[\s\S]*?\*/)"), "");
  // remove all spaces and newlines
  code = std::regex_replace(code, std::regex(R"(\s+)"), "");

  auto expected = retType + name + "(";
  return code.find(expected) != std::string::npos;
}

uint32_t Utils::CPP::calcStructSize(const Struct&s) {
  uint32_t size = 0;
  for(const auto &field : s.fields) {
    size += field.dataSize;
  }
  return size;
}

