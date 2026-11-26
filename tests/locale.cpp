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

// ---------------------------------------------------------------- phase 2: tags, variables, formatting

#include "text/locFormat.h"
#include "project/locale/locCompile.h"

P64_LOC_VAR(testCoins, Int) { return 1234; }
P64_LOC_VAR(testItem, Key) { return "test.sword"_localized; }
P64_LOC_VAR(testName, Text) { return "R$tar"; }
P64_LOC_ARG(testCount, Int);

namespace
{
  struct Fixture
  {
    Config cfg{};
    CompileEnv env{};
    P64::Loc::FontSet sets[2]{};
    P64::Loc::FormatEnv fenv{};
    std::string sword{};

    Fixture()
    {
      std::vector<std::string> errors{};
      parseConfig(nlohmann::json::parse(R"({
        "source": "en",
        "languages": [{"code": "en", "fonts": "latin"}, {"code": "ja", "fonts": "latin", "wrap": "char"}],
        "colors": {"red": "#ff0000", "place": "#3080ff"},
        "effects": ["shake", "wave"],
        "controls": {"wait": "seconds", "speed": "name", "sfx": "name"},
        "fontSets": {"latin": {"regular": "a.ttf", "bold": "b.ttf", "sizes": [12, 16], "base": 16}, "icons": {"regular": "i.fnt"}},
        "icons": {"set": "icons", "base": 57344, "names": ["a", "b"]}
      })"), cfg, errors);
      CHECK(errors.empty());
      VarDecls vars{};
      vars["testCoins"] = {.type = "Int"};
      vars["testItem"] = {.type = "Key"};
      vars["testName"] = {.type = "Text"};
      vars["testCount"] = {.type = "Int", .isArg = true};
      env = makeCompileEnv(cfg, vars, errors);
      CHECK(errors.empty());

      // font set 0 "icons", 1 "latin" (sets are sorted by name)
      sets[0] = {"icons", {{30, 30, 0, 0}, {}, {}, {}}, 1, 0, 0};
      sets[1] = {"latin", {{16, 18, 0, 0}, {17, 19, 0, 0}, {}, {}}, 2, 1, 0};
      fenv.sets = sets;
      fenv.setCount = 2;
      fenv.baseSet = 1;
      fenv.iconSet = 0;
      fenv.iconBase = 0xE000;
      std::vector<std::string> e2{};
      sword = compileText("Sword", env, P64::Loc::PluralRule::OneOther, e2);
      fenv.user = this;
      fenv.keyText = [](uint32_t id, bool*, void* user) -> const char* {
        auto* self = static_cast<Fixture*>(user);
        return id == "test.sword"_localized.hash ? self->sword.c_str() : nullptr;
      };
    }

    std::string run(const std::string &src, P64::Loc::PluralRule rule = P64::Loc::PluralRule::OneOther, const P64::Loc::Args* args = nullptr)
    {
      std::vector<std::string> errors{};
      const auto compiled = compileText(src, env, rule, errors);
      CHECK_MESSAGE(errors.empty(), src);
      char out[256];
      auto e = fenv;
      e.plural = rule;
      e.args = args;
      P64::Loc::formatText(compiled.c_str(), false, out, sizeof(out), e);
      return out;
    }

    std::vector<std::string> errorsOf(const std::string &src)
    {
      std::vector<std::string> errors{};
      compileText(src, env, P64::Loc::PluralRule::OneOther, errors);
      return errors;
    }
  };

  std::string cp(uint32_t c)
  {
    char buf[4];
    int n = P64::Loc::utf8Encode(c, buf, 4);
    return std::string(buf, n);
  }
}

TEST_CASE("loc format: fonts, bold and sizes become rdpq codes")
{
  Fixture f{};
  CHECK(f.run("Hello [b]you[/b]!") == "$12^00Hello $13^00you$12^00!");
  CHECK(f.run("[size=small]tiny") == "$10^00tiny");
  CHECK(f.run("[size=big]big") == "$12^00big"); // clamped to the largest size
  f.fenv.sizeStep = -1;                      // the text size setting
  CHECK(f.run("A[b]B") == "$10^00A$11^00B");
}

TEST_CASE("loc format: colors and effects are style ids")
{
  Fixture f{};
  f.fenv.baseSet = 0xFF; // no fonts: style codes only
  // palette: 0 default, 1 place, 2 red (sorted by name); effects: 1 shake, 2 wave
  CHECK(f.run("[color=red]Hi[/color] you") == "^02Hi^00 you");
  CHECK(f.run("[shake]Boo[/shake]") == "^20Boo"); // a code is only written before text
  CHECK(f.run("[color=place][wave]x") == "^41x");
  CHECK(f.run("[color=#00ff00]g") == "^03g"); // a custom color joins the palette
}

TEST_CASE("loc format: variables, numbers and plurals")
{
  Fixture f{};
  f.fenv.baseSet = 0xFF;
  CHECK(f.run("{testCoins}") == "1,234");
  CHECK(f.run("{testCoins:n}") == "1234");
  CHECK(f.run("{testCoins:06}") == "001234");
  f.fenv.groupSep = '.';
  f.fenv.groupMin = 5;
  CHECK(f.run("{testCoins}") == "1234"); // Spanish groups from 10.000
  f.fenv.groupSep = ',';
  f.fenv.groupMin = 4;

  CHECK(f.run("You got {testItem}!") == "You got Sword!");
  CHECK(f.run("{testName}") == "R$$tar"); // raw text is made safe for rdpq

  const std::string coins = "{testCount, plural, =0 {no coins} one {# coin} other {# coins}}";
  P64::Loc::Args args{};
  args.set("testCount"_var, 0);
  CHECK(f.run(coins, P64::Loc::PluralRule::OneOther, &args) == "no coins");
  args.items[0].v.i = 1;
  CHECK(f.run(coins, P64::Loc::PluralRule::OneOther, &args) == "1 coin");
  args.items[0].v.i = 5;
  CHECK(f.run(coins, P64::Loc::PluralRule::OneOther, &args) == "5 coins");
  args.items[0].v.i = 1;
  CHECK(f.run(coins, P64::Loc::PluralRule::OtherOnly, &args) == "1 coins"); // Japanese has one form

  // the caller's value wins over the global one
  P64::Loc::Args mine{};
  mine.set("testCoins"_var, 7);
  CHECK(f.run("{testCoins}", P64::Loc::PluralRule::OneOther, &mine) == "7");
}

TEST_CASE("loc format: controls, icons, pages and escapes")
{
  Fixture f{};
  f.fenv.baseSet = 0xFF;
  // controls are sorted by name: sfx 1, speed 2, wait 3
  CHECK(f.run("a[wait=0.5]b") == "a" + cp(P64::Loc::control(3, 50)) + "b");
  CHECK(f.run("[speed=slow]x[speed=fast]y") == cp(P64::Loc::control(2, 0)) + "x" + cp(P64::Loc::control(2, 1)) + "y");
  CHECK(f.run("one[page]two") == "one" + cp(P64::Loc::control(0, P64::Loc::CTRL_PAGE)) + "two");
  CHECK(f.run("Press [btn=b]") == "Press $1E" + cp(0xE001));
  CHECK(f.run("[[not a tag] {{x} $5 ^_^ a\\nb") == "[not a tag] {x} $$5 ^^_^^ a\nb");
  REQUIRE(f.env.controls.size() == 3);
  CHECK(f.env.controls[1].names.size() == 2); // slow, fast
}

TEST_CASE("loc compile: mistakes are errors")
{
  Fixture f{};
  CHECK(f.errorsOf("[blink]x").size() == 1);
  CHECK(f.errorsOf("x[/b]").size() == 1);
  CHECK(f.errorsOf("{nobody}").size() == 1);
  CHECK(f.errorsOf("{testName:03}").size() == 1);
  CHECK(f.errorsOf("{testCount, plural, one {#}}").size() == 1); // no other
  CHECK(f.errorsOf("[color=blurple]").size() == 1);
  CHECK(f.errorsOf("[wait=abc]").size() == 1);
  CHECK(f.errorsOf("[btn=start]").size() == 1);
  CHECK(f.errorsOf("open [b").size() == 1);
  CHECK(f.errorsOf("[b]open at the end is fine").empty());
}

TEST_CASE("loc format: untranslated text is tinted, a cut never splits a code")
{
  Fixture f{};
  f.fenv.baseSet = 0xFF;
  std::vector<std::string> errors{};
  const auto compiled = compileText("$$$$$$", f.env, P64::Loc::PluralRule::OneOther, errors);
  char out[6];
  auto res = P64::Loc::formatText(compiled.c_str(), false, out, sizeof(out), f.fenv);
  CHECK(res.truncated);
  CHECK(std::string(out) == "$$$$"); // pairs only

  auto e = f.fenv;
  e.fallbackColor = 5;
  char out2[32];
  P64::Loc::formatText("Hi", true, out2, sizeof(out2), e);
  CHECK(std::string(out2) == "^05Hi");
}

TEST_CASE("loc compile: glyphs, variable declarations, kinsoku")
{
  Fixture f{};
  std::vector<std::string> errors{};
  const auto compiled = compileText("[b]あ[/b]{testCoins}[wait=1]い", f.env, P64::Loc::PluralRule::OneOther, errors);
  std::set<uint32_t> glyphs{};
  collectGlyphs(compiled, glyphs);
  CHECK(glyphs == std::set<uint32_t>{0x3042, 0x3044});

  VarDecls vars{};
  scanVarDecls("// P64_LOC_VAR(commented, Int)\n#define P64_LOC_VAR(NAME, TYPE) x\nP64_LOC_VAR(coins, Int) { return 1; }\nP64_LOC_ARG(price, Int);\nP64_LOC_VAR(bad, Float) {}\n",
    "v.cpp", vars, errors);
  CHECK(vars.size() == 2);
  CHECK(vars["coins"].type == "Int");
  CHECK(vars["price"].isArg);
  CHECK(errors.size() == 1);

  CHECK(P64::Loc::kinsokuNoStart(0x3002)); // 。
  CHECK(P64::Loc::kinsokuNoStart(0x3063)); // っ
  CHECK(P64::Loc::kinsokuNoEnd(0x300C));   // 「
  CHECK_FALSE(P64::Loc::kinsokuNoStart(0x3042));
  CHECK(P64::Loc::pluralCategory(P64::Loc::PluralRule::French, 0) == P64::Loc::PluralCat::One);
  CHECK(P64::Loc::pluralCategory(P64::Loc::PluralRule::Spanish, 1000000) == P64::Loc::PluralCat::Many);
}
