/**
* @license MIT
*/
// Script data parsing the Full-Frontal fork adds on top of upstream's (see codeParser.cpp tests):
// P64::Enum, implicit and hex values, range checks, stacked attribute blocks, string literal values.
#include "doctest/doctest.h"
#include <string>
#include <utility>
#include <vector>
#include "utils/codeParser.h"
#include "utils/logger.h"

using Utils::DataType;
using Utils::CPP::Struct;

namespace
{
  Struct parseData(const std::string &body) {
    return Utils::CPP::parseDataStruct("P64_DATA(\n" + body + "\n);", "Data");
  }

  bool logContains(const std::string &text) {
    return Utils::Logger::getLog().find(text) != std::string::npos;
  }

  using EnumValues = std::vector<std::pair<int64_t, std::string>>;
  using BitValues = std::vector<std::pair<int, std::string>>;
}

TEST_SUITE("parseDataStruct (fork)")
{
  TEST_CASE("emitter asset references")
  {
    auto s = parseData("AssetRef<emitter_t> fx;");
    REQUIRE(s.fields.size() == 1);
    CHECK(s.fields[0].type == DataType::ASSET_EMITTER);
    CHECK(s.fields[0].dataSize == 4);
  }

  TEST_CASE("enum attribute")
  {
    Utils::Logger::clear();
    auto s = parseData(R"(
      [[P64::Enum("0=Auto, 1=Loop, 3=Once")]] uint8_t mode;
      [[P64::Enum("Off, On, Blink")]] uint8_t implicit;
      [[P64::Enum("-1=Back, 0x10=Hex, Next")]] int8_t mixed;
      [[P64::Enum("1=Ok, 300=Big, 1.5=Half")]] uint8_t ranged;
      [[P64::Enum("0=Off")]] float notInteger;
    )");
    REQUIRE(s.fields.size() == 5);
    CHECK(s.fields[0].enumValues == EnumValues{{0, "Auto"}, {1, "Loop"}, {3, "Once"}});
    CHECK(s.fields[1].enumValues == EnumValues{{0, "Off"}, {1, "On"}, {2, "Blink"}});
    CHECK(s.fields[2].enumValues == EnumValues{{-1, "Back"}, {16, "Hex"}, {17, "Next"}});
    // 300 does not fit into a uint8_t, 1.5 is not an integer: both skipped with a warning
    CHECK(s.fields[3].enumValues == EnumValues{{1, "Ok"}});
    CHECK(logContains("out of range"));
    CHECK(logContains("not a valid number"));
    CHECK(s.fields[4].enumValues.empty());
  }

  TEST_CASE("bitmask entries continue from the previous one and must fit the member")
  {
    auto s = parseData(R"(
      [[P64::Bitmask("Fire, Water, 4=Earth, Air")]] uint8_t flags;
      [[P64::Bitmask("0=Low, 8=TooHigh")]] uint8_t small;
      [[P64::Bitmask("0x1F=Top")]] uint32_t wide;
    )");
    REQUIRE(s.fields.size() == 3);
    CHECK(s.fields[0].bitmask == BitValues{{0, "Fire"}, {1, "Water"}, {4, "Earth"}, {5, "Air"}});
    CHECK(s.fields[1].bitmask == BitValues{{0, "Low"}});
    CHECK(s.fields[2].bitmask == BitValues{{31, "Top"}});
  }

  TEST_CASE("stacked attribute blocks are merged")
  {
    auto s = parseData(R"(
      [[P64::Name("Flags")]]
      [[P64::Bitmask("0=A, 1=B")]]
      uint8_t flags;
      [[P64::Name("First")]] [[P64::Name("Second")]] int32_t later;
    )");
    REQUIRE(s.fields.size() == 2);
    CHECK(s.fields[0].attr.at("P64::Name") == "Flags");
    CHECK(s.fields[0].bitmask == BitValues{{0, "A"}, {1, "B"}});
    CHECK(s.fields[1].attr.at("P64::Name") == "Second"); // later blocks win
  }

  TEST_CASE("string literal values keep brackets, commas and escaped quotes")
  {
    auto s = parseData(R"x(
      [[P64::Name("a]b, c)")]] int32_t bracket;
      [[P64::Name("say \"hi\"")]] int32_t quoted;
      [[P64::Name("Mode"), P64::Enum("0=A, 1=B")]] uint8_t both;
    )x");
    REQUIRE(s.fields.size() == 3);
    CHECK(s.fields[0].attr.at("P64::Name") == "a]b, c)");
    CHECK(s.fields[1].attr.at("P64::Name") == "say \"hi\"");
    CHECK(s.fields[2].attr.at("P64::Name") == "Mode");
    CHECK(s.fields[2].enumValues == EnumValues{{0, "A"}, {1, "B"}});
  }
}
