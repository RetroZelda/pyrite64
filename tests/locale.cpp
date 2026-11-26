/**
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, section 4). Keys and ids, the tables the editor
// keeps, the scan of C++ source, the pseudo-language, and the table file the console reads.
#include "doctest/doctest.h"
#include <cstring>
#include <string>

#include "text/locId.h"
#include "text/locTable.h"
#include "project/locale/localization.h"

using namespace Project::Loc;

TEST_CASE("loc: the literal and keyId agree, and are CRC-32")
{
  constexpr auto id = "npc.tank.greet"_localized;
  static_assert(id.hash == P64::Loc::keyId("npc.tank.greet", 14).hash);
  // CRC-32 of "a" is e8b7be43
  static_assert("a"_localized.hash == 0xE8B7BE43u);
  CHECK(id.hash != 0);
  CHECK(sourceHash("a") == 0xE8B7BE43u);
}

TEST_CASE("loc: key rules")
{
  CHECK(isValidKey("hud.verb.talk"));
  CHECK(isValidKey("a"));
  CHECK(isValidKey("dlg.tree_1.0"));
  std::string why{};
  CHECK_FALSE(isValidKey("", &why));
  CHECK_FALSE(isValidKey("Hud.talk", &why));
  CHECK(why.find("a-z") != std::string::npos);
  CHECK_FALSE(isValidKey("hud..talk"));
  CHECK_FALSE(isValidKey(".hud"));
  CHECK_FALSE(isValidKey("hud."));
  CHECK_FALSE(isValidKey("hud talk"));
}

TEST_CASE("loc: comments are blanked, strings and line numbers kept")
{
  const std::string code =
    "// \"a.b\"_localized in a comment\n"
    "auto x = \"hud.ok\"_localized; /* \"c.d\"_localized\n"
    "   still a comment */ auto y = \"// not a comment\";\n"
    "auto z = R\"(\"e.f\"_localized)\";\n"
    "int n = 1'000; auto w = \"npc.wife.plea\"_localized;\n";
  Uses uses{};
  std::vector<std::string> errors{};
  scanCpp(code, "x.cpp", uses, errors);
  CHECK(errors.empty());
  CHECK(uses.size() == 2);
  REQUIRE(uses.contains("hud.ok"));
  CHECK(uses["hud.ok"][0].line == 2);
  REQUIRE(uses.contains("npc.wife.plea"));
  CHECK(uses["npc.wife.plea"][0].line == 5);
  CHECK_FALSE(uses.contains("a.b"));
  CHECK_FALSE(uses.contains("c.d"));

  const auto blank = blankComments(code);
  CHECK(blank.size() == code.size());
  CHECK(blank.find("// not a comment") != std::string::npos);
}

TEST_CASE("loc: a malformed key in C++ is an error, not a key")
{
  Uses uses{};
  std::vector<std::string> errors{};
  scanCpp("auto a = \"Bad.Key\"_localized;\n", "y.cpp", uses, errors);
  CHECK(uses.empty());
  REQUIRE(errors.size() == 1);
  CHECK(errors[0].find("y.cpp:1") != std::string::npos);
}

namespace
{
  Localization makeLoc()
  {
    Localization loc{};
    std::vector<std::string> errors{};
    auto doc = nlohmann::json::parse(R"({
      "source": "en",
      "languages": [
        {"code": "ja", "name": "日本語", "wrap": "char"},
        {"code": "en", "name": "English"},
        {"code": "qps", "name": "Pseudo", "pseudo": true}
      ],
      "pairs": {"en": "ja", "ja": "en"},
      "keep": ["item.*"],
      "fontSets": {"latin": {"regular": "x.ttf"}}
    })");
    parseConfig(doc, loc.config, errors);
    CHECK(errors.empty());
    for(const auto &l : loc.config.languages) loc.tables.push_back({.code = l.code});
    return loc;
  }
}

TEST_CASE("loc: config puts the source language first and keeps unknown parts")
{
  auto loc = makeLoc();
  REQUIRE(loc.config.languages.size() == 3);
  CHECK(loc.config.languages[0].code == "en");
  CHECK(loc.config.languages[1].code == "ja");
  CHECK(loc.config.languages[1].wrap == "char");
  CHECK(loc.config.pairOf("en") == "ja");
  CHECK(loc.config.keeps("item.sword"));
  CHECK_FALSE(loc.config.keeps("items.sword"));

  auto text = serializeConfig(loc.config);
  CHECK(text.find("fontSets") != std::string::npos);
  CHECK(text.find("日本語") != std::string::npos); // not escaped
}

TEST_CASE("loc: refresh adds used keys to every stored table, unused excludes kept keys")
{
  auto loc = makeLoc();
  Uses uses{};
  uses["hud.verb.talk"].push_back({"C++", "a.cpp", 1});
  uses["npc.tank.greet"].push_back({"C++", "a.cpp", 2});
  auto added = loc.addMissing(uses);
  CHECK(added.size() == 2);
  CHECK(loc.tables[0].entries.size() == 2);
  CHECK(loc.tables[1].entries.size() == 2);
  CHECK(loc.tables[2].entries.empty()); // pseudo: never stored

  CHECK(loc.status("hud.verb.talk", 0) == Status::MissingSource);
  CHECK(loc.status("hud.verb.talk", 1) == Status::Missing);
  CHECK(loc.status("hud.verb.talk", 2) == Status::Ok);

  loc.setText("hud.verb.talk", 0, "TALK");
  loc.setText("hud.verb.talk", 1, "はなす");
  CHECK(loc.status("hud.verb.talk", 0) == Status::Ok);
  CHECK(loc.status("hud.verb.talk", 1) == Status::Ok);

  // rewording the English flags the translation
  loc.setText("hud.verb.talk", 0, "SPEAK");
  CHECK(loc.status("hud.verb.talk", 1) == Status::NeedsReview);
  loc.confirm("hud.verb.talk", 1);
  CHECK(loc.status("hud.verb.talk", 1) == Status::Ok);

  // unused: not in the uses, not kept
  loc.tables[0].entries["old.line"] = {.text = "Old"};
  loc.tables[0].entries["item.sword"] = {.text = "Sword"};
  loc.tables[0].entries["pinned"] = {.text = "Pinned", .keep = true};
  auto unused = loc.unused(uses);
  REQUIRE(unused.size() == 1);
  CHECK(unused[0] == "old.line");
  loc.remove(unused);
  CHECK_FALSE(loc.tables[0].entries.contains("old.line"));
}

TEST_CASE("loc: tables round-trip, sorted, with notes and from")
{
  Table t{.code = "ja"};
  t.entries["b.key"] = {.text = "ビー", .from = 0x1234ABCDu};
  t.entries["a.key"] = {.text = "エー", .note = "n", .keep = true};
  const auto text = serializeTable(t);
  CHECK(text.find("a.key") < text.find("b.key"));
  CHECK(text.find("1234abcd") != std::string::npos);

  Table back{.code = "ja"};
  std::vector<std::string> errors{};
  parseTable(nlohmann::json::parse(text), back, errors);
  CHECK(errors.empty());
  CHECK(back.entries["b.key"].from == 0x1234ABCDu);
  CHECK(back.entries["a.key"].keep);
  CHECK(back.entries["a.key"].text == "エー");
}

TEST_CASE("loc: pseudo text keeps tags and variables, and is longer")
{
  const auto p = pseudoText("Hi [b]{name}[/b], you have {coins, plural, one {# coin} other {# coins}}. [[x]");
  CHECK(p.front() == '[');
  CHECK(p.back() == ']');
  CHECK(p.find("[b]") != std::string::npos);
  CHECK(p.find("[/b]") != std::string::npos);
  CHECK(p.find("{name}") != std::string::npos);
  CHECK(p.find("{coins, plural, one {") != std::string::npos);
  CHECK(p.find("[[ẋ]") != std::string::npos); // [[ is a literal [, the x after it is text
  CHECK(p.find("Ĥî") != std::string::npos);
  CHECK(p.find("#") != std::string::npos);
  CHECK(p.find("# coin}") == std::string::npos); // the plural forms' words are changed, the variable name is not
}

TEST_CASE("loc: '$' and '^' are doubled for rdpq")
{
  CHECK(escapeForRdpq("$5 ^_^") == "$$5 ^^_^^");
}

TEST_CASE("loc: a written table is read back by the engine's reader")
{
  std::vector<BuiltEntry> entries{
    {.id = P64::Loc::keyId("b", 1).hash, .text = "Bee"},
    {.id = P64::Loc::keyId("a", 1).hash, .text = "Ay", .flags = P64::Loc::ENTRY_FALLBACK},
    {.id = P64::Loc::keyId("c", 1).hash, .text = "Bee"}, // same text: stored once
  };
  auto bytes = writeTable(entries, P64::Loc::FILE_PSEUDO);
  CHECK(bytes.size() % 4 == 0);

  P64::Loc::TableView view{};
  REQUIRE(view.init(bytes.data(), static_cast<uint32_t>(bytes.size())));
  CHECK(view.count == 3);
  CHECK(view.fileFlags == P64::Loc::FILE_PSEUDO);

  auto ia = view.find("a"_localized.hash);
  auto ib = view.find("b"_localized.hash);
  auto ic = view.find("c"_localized.hash);
  REQUIRE(ia >= 0);
  REQUIRE(ib >= 0);
  REQUIRE(ic >= 0);
  CHECK(std::strcmp(view.textAt(ia), "Ay") == 0);
  CHECK(std::strcmp(view.textAt(ib), "Bee") == 0);
  CHECK(view.textAt(ib) == view.textAt(ic));
  CHECK(view.flagsAt(ia) == P64::Loc::ENTRY_FALLBACK);
  CHECK(view.flagsAt(ib) == 0);
  CHECK(view.find("zzz"_localized.hash) == -1);

  // bad data is refused
  bytes[0] = 'X';
  CHECK_FALSE(view.init(bytes.data(), static_cast<uint32_t>(bytes.size())));
  CHECK_FALSE(view.init(bytes.data(), 4));
}
