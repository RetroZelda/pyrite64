/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>
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
  };

  struct Struct {
    std::string name;
    std::vector<Field> fields;
  };

  Struct parseDataStruct(const std::string &sourceCode, const std::string &structName);

  bool hasFunction(const std::string &sourceCode, const std::string &retType, const std::string &name);

  uint32_t calcStructSize(const Struct &s);
}
