/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "binaryFile.h"

namespace Utils::CPP
{
  struct Field {
    DataType type;
    uint32_t dataSize;
    std::string name;
    std::unordered_map<std::string, std::string> attr;
    std::string defaultValue;
    // Parsed [[P64::Bitmask("0=Fire, 1=Water")]] entries as (bit-index, name) pairs.
    // Only populated for unsigned integer fields that carry the attribute.
    std::vector<std::pair<int, std::string>> bitmask;
    // Parsed [[P64::Enum("0=Auto, 1=Loop")]] entries as (value, name) pairs.
    // Only populated for integer fields that carry the attribute.
    // 64-bit, so every value of the member type fits (e.g. 3000000000 for a uint32_t).
    std::vector<std::pair<int64_t, std::string>> enumValues;
    // Numeric bounds from [[P64::Range(min, max)]], [[P64::Min(x)]] or [[P64::Max(x)]].
    // Only populated for integer/float fields. 'slider' is set by Range, in which case
    // both bounds are present and the editor shows a slider instead of a clamped input.
    std::optional<float> min{};
    std::optional<float> max{};
    bool slider{false};
    std::string group{};

    [[nodiscard]] bool isNumeric() const {
      return type == DataType::u8 || type == DataType::s8 || type == DataType::u16 ||
             type == DataType::s16 || type == DataType::u32 || type == DataType::s32 ||
             type == DataType::f32;
    }
  };

  struct Struct {
    std::string name;
    std::vector<Field> fields;
  };

  Struct parseDataStruct(const std::string &sourceCode, const std::string &structName);

  /**
   * Parses the data of an object script, either inline ('P64_DATA') or a struct named by 'P64_DATA_TYPE'.
   * The struct is searched in the script itself and recursively in its quoted includes,
   * relative to the including file first and then to each of the include directories.
   */
  Struct parseScriptData(
    const std::string &sourceCode,
    const std::filesystem::path &sourcePath,
    const std::vector<std::filesystem::path> &includeDirs
  );

  bool hasFunction(const std::string &sourceCode, const std::string &retType, const std::string &name);

  // Fork: event graph. Pins an object script declares with P64_EVENTS_IN(...) / P64_EVENTS_OUT(...).
  struct EventPins
  {
    std::vector<std::string> in{};
    std::vector<std::string> out{};
    std::vector<std::string> errors{}; // problems found, one sentence each

    [[nodiscard]] bool empty() const { return in.empty() && out.empty(); }
  };

  // Fork: event graph. Pins a global script declares for one system with
  // P64_GLOBAL_EVENTS_IN(System, ...) / P64_GLOBAL_EVENTS_OUT(System, ...).
  struct SystemPins
  {
    std::string system{};
    std::vector<std::string> in{};
    std::vector<std::string> out{};
  };

  EventPins parseEventPins(const std::string &sourceCode);

  // Fork: conditions (docs/todo/conditionals/PLAN.md). What an object script declares with
  // P64_CONDITION_DATA(...), P64_CONDITION_TRIGGERS(...) and P64_CONDITION_INPUTS(...).
  struct Condition
  {
    bool isCondition{false};             // the script declares its data with P64_CONDITION_DATA
    std::vector<std::string> triggers{}; // event names that make it check itself, as written
    std::vector<std::string> inputs{};   // its input pins that read conditions
    std::vector<std::string> errors{};   // problems found, one sentence each

    [[nodiscard]] bool readsConditions(const std::string &pin) const;
  };

  /** 'pins' are the script's own pins (parseEventPins of the same source). */
  Condition parseCondition(const std::string &sourceCode, const EventPins &pins);

  // Fork: interactions (docs/todo/interactions/PLAN.md). What an object script asks the editor to
  // draw in the viewport: P64_EDITOR_ARROW(), P64_EDITOR_ARROW(r, g, b) or (r, g, b, length).
  struct EditorHints
  {
    bool arrow{false};
    float arrowColor[3]{1.0f, 0.6f, 0.1f};
    float arrowLength{8.0f};               // in world units, times the object's scale
    std::vector<std::string> errors{};
  };

  EditorHints parseEditorHints(const std::string &sourceCode);

  /**
   * The fields P64_CONDITION_DATA puts in front of a condition script's own (Conditions::Header).
   * "Auto Check" is only offered (has a P64::Name) when the script names triggers.
   */
  std::string conditionHeaderFields(bool hasTriggers);

  /** Systems in declaration order, IN and OUT of one system merged. Problems go to 'errors'. */
  std::vector<SystemPins> parseGlobalEventPins(const std::string &sourceCode, std::vector<std::string> *errors = nullptr);

  uint32_t calcStructSize(const Struct &s);
}
