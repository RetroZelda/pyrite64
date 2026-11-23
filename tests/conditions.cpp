/**
* @license MIT
*/
// Fork: conditions (docs/todo/conditionals/PLAN.md). The rules of a check, the provider table,
// and what the editor reads from a condition script.
#include "doctest/doctest.h"
#include <cstring>
#include <string>

#include "events/conditionTypes.h"
#include "events/conditionCore.h"
#include "events/pins.h"
#include "utils/codeParser.h"

using namespace P64::Conditions;
using P64::Events::Scope;

TEST_CASE("condition: starts false, so the first true answer is newly true")
{
  Header header{};
  reset(header);
  CHECK(isEnabled(header));

  auto first = applyAnswer(header, true);
  CHECK(first.checked);
  CHECK(first.answer);
  CHECK(first.changed);

  auto again = applyAnswer(header, true);
  CHECK(again.checked);
  CHECK(again.answer);
  CHECK_FALSE(again.changed);

  auto dropped = applyAnswer(header, false);
  CHECK_FALSE(dropped.answer);
  CHECK(dropped.changed);

  auto stillFalse = applyAnswer(header, false);
  CHECK_FALSE(stillFalse.changed);
}

TEST_CASE("condition: a first false answer is not a change")
{
  Header header{};
  reset(header);
  auto first = applyAnswer(header, false);
  CHECK(first.checked);
  CHECK_FALSE(first.answer);
  CHECK_FALSE(first.changed);
}

TEST_CASE("condition: disabled, a check does nothing and remembers nothing")
{
  Header header{};
  reset(header);
  setEnabled(header, false);
  CHECK_FALSE(isEnabled(header));

  auto ignored = applyAnswer(header, true);
  CHECK_FALSE(ignored.checked);
  CHECK_FALSE(ignored.changed);

  // enabled again, the answer it missed is still new
  setEnabled(header, true);
  auto seen = applyAnswer(header, true);
  CHECK(seen.checked);
  CHECK(seen.changed);
}

TEST_CASE("condition: reset forgets the answer and returns to Starts Enabled")
{
  Header header{};
  reset(header);
  applyAnswer(header, true);
  setEnabled(header, false);
  header.flags |= Flag::PENDING;

  reset(header);
  CHECK(isEnabled(header));
  CHECK((header.flags & Flag::PENDING) == 0);
  CHECK(applyAnswer(header, true).changed); // false again after the reset, so true is new

  Header off{.startsEnabled = 0};
  reset(off);
  CHECK_FALSE(isEnabled(off));
  CHECK_FALSE(applyAnswer(off, true).checked);
}

TEST_CASE("condition: the header is the four bytes the editor writes in front of the fields")
{
  CHECK(sizeof(Header) == 4);
  CHECK(offsetof(Header, startsEnabled) == 0);
  CHECK(offsetof(Header, flags) == 1);

  // the same fields, as the editor's parser sees them
  CHECK(offsetof(Header, autoCheck) == 2);
  auto fields = Utils::CPP::parseDataStruct(
    "P64_CONDITION_DATA(\n [[P64::Name(\"Value\")]] float value = 50.0f;\n);", "Data").fields;
  REQUIRE(fields.size() == 5);
  CHECK(fields[0].name == "conditionStartsEnabled");
  CHECK(fields[0].attr.at("P64::Name") == "Starts Enabled");
  CHECK(fields[0].defaultValue == "1"); // on by default
  CHECK(fields[1].name == "conditionFlags");
  CHECK(fields[1].attr.find("P64::Name") == fields[1].attr.end()); // hidden in the inspector
  CHECK(fields[2].name == "conditionAutoCheck");
  CHECK(fields[2].defaultValue == "1");
  CHECK(fields[3].name == "conditionReserved");
  CHECK(fields[4].name == "value");
  uint32_t headerSize = fields[0].dataSize + fields[1].dataSize + fields[2].dataSize + fields[3].dataSize;
  CHECK(headerSize == sizeof(Header));
}

TEST_CASE("condition: Auto Check is only offered when the script names triggers")
{
  auto plain = Utils::CPP::parseDataStruct("P64_CONDITION_DATA();\nbool evaluate(Object& obj, Data *data) { return true; }", "Data").fields;
  REQUIRE(plain.size() == 4);
  CHECK(plain[2].name == "conditionAutoCheck");
  CHECK(plain[2].attr.find("P64::Name") == plain[2].attr.end()); // nothing to switch off: hidden

  auto empty = Utils::CPP::parseDataStruct("P64_CONDITION_DATA();\nP64_CONDITION_TRIGGERS();", "Data").fields;
  REQUIRE(empty.size() == 4);
  CHECK(empty[2].attr.find("P64::Name") == empty[2].attr.end());

  auto triggered = Utils::CPP::parseDataStruct(
    "P64_CONDITION_DATA();\nP64_CONDITION_TRIGGERS(\"scene::Loaded\"_event);", "Data").fields;
  REQUIRE(triggered.size() == 4);
  CHECK(triggered[2].attr.at("P64::Name") == "Auto Check");
  CHECK(triggered[2].defaultValue == "1");
}

TEST_CASE("condition: a script without fields still has the header")
{
  auto fields = Utils::CPP::parseDataStruct("P64_CONDITION_DATA();\nbool evaluate(Object& obj, Data *data) { return true; }", "Data").fields;
  REQUIRE(fields.size() == 4);
  CHECK(fields[0].name == "conditionStartsEnabled");
}

TEST_CASE("condition: P64_DATA scripts are parsed as before")
{
  auto fields = Utils::CPP::parseDataStruct("P64_DATA(\n float speed = 2.0f;\n);", "Data").fields;
  REQUIRE(fields.size() == 1);
  CHECK(fields[0].name == "speed");
}

TEST_CASE("condition pins: names, order and index base are what the runtime switches on")
{
  using namespace P64::Events::Pins;
  CHECK(CONDITION_PIN_BASE == 0x80);
  REQUIRE(CONDITION_IN_LIST.count == 4);
  REQUIRE(CONDITION_OUT_LIST.count == 5);
  CHECK(std::string{CONDITION_IN[(int)ConditionIn::Enable]} == "Enable");
  CHECK(std::string{CONDITION_IN[(int)ConditionIn::Disable]} == "Disable");
  CHECK(std::string{CONDITION_IN[(int)ConditionIn::Check]} == "Check");
  CHECK(std::string{CONDITION_IN[(int)ConditionIn::Reset]} == "Reset");
  CHECK(std::string{CONDITION_OUT[(int)ConditionOut::True]} == "True");
  CHECK(std::string{CONDITION_OUT[(int)ConditionOut::False]} == "False");
  CHECK(std::string{CONDITION_OUT[(int)ConditionOut::NewlyTrue]} == "NewlyTrue");
  CHECK(std::string{CONDITION_OUT[(int)ConditionOut::NewlyFalse]} == "NewlyFalse");
  CHECK(std::string{CONDITION_OUT[(int)ConditionOut::Result]} == "Result");
}

TEST_CASE("providers: found by name, scope and instance, in the order they were added")
{
  Core::Providers providers{};
  CHECK(providers.add(0x1111, Scope::Scene, 0, 10, 1));
  CHECK(providers.add(0x1111, Scope::Scene, 0, 11, 2));
  CHECK(providers.add(0x1111, Scope::Prefab, 40, 12, 1)); // the same name inside a prefab instance
  CHECK(providers.add(0x2222, Scope::Scene, 0, 13, 1));
  CHECK(providers.count() == 4);

  Core::Provider found[8];
  REQUIRE(providers.find(0x1111, Scope::Scene, 0, found, 8) == 2);
  CHECK(found[0].object == 10);
  CHECK(found[0].comp == 1);
  CHECK(found[1].object == 11);
  CHECK(found[1].comp == 2);

  REQUIRE(providers.find(0x1111, Scope::Prefab, 40, found, 8) == 1);
  CHECK(found[0].object == 12);
  CHECK(providers.find(0x1111, Scope::Prefab, 41, found, 8) == 0); // another instance of the prefab
  CHECK(providers.find(0x3333, Scope::Scene, 0, found, 8) == 0);
  CHECK(providers.find(0x1111, Scope::Scene, 0, found, 1) == 1);   // never more than asked for
}

TEST_CASE("providers: adding twice keeps one entry, removing takes one script's entries")
{
  Core::Providers providers{};
  CHECK(providers.add(0x1111, Scope::Scene, 0, 10, 1));
  CHECK(providers.add(0x1111, Scope::Scene, 0, 10, 1));
  CHECK(providers.count() == 1);

  CHECK(providers.add(0x2222, Scope::Scene, 0, 10, 1)); // its Result bound to a second name
  CHECK(providers.add(0x1111, Scope::Scene, 0, 10, 3)); // a second condition script on the object
  CHECK(providers.add(0x1111, Scope::Scene, 0, 11, 1));
  CHECK(providers.count() == 4);

  CHECK(providers.remove(10, 1) == 2);
  CHECK(providers.count() == 2);
  Core::Provider found[8];
  REQUIRE(providers.find(0x1111, Scope::Scene, 0, found, 8) == 2);
  CHECK(found[0].object == 10);
  CHECK(found[0].comp == 3);
  CHECK(found[1].object == 11);
  CHECK(providers.remove(99, 1) == 0);

  providers.clear();
  CHECK(providers.count() == 0);
  CHECK(providers.peak() == 4);
}

TEST_CASE("providers: a full table refuses, and an entry already there still counts as added")
{
  Core::Providers providers{};
  for(uint32_t i = 0; i < Core::Providers::MAX; ++i) {
    REQUIRE(providers.add(0x1000 + i, Scope::Scene, 0, (uint16_t)(i + 1), 1));
  }
  CHECK_FALSE(providers.add(0x9999, Scope::Scene, 0, 500, 1));
  CHECK(providers.add(0x1000, Scope::Scene, 0, 1, 1));
  CHECK(providers.count() == Core::Providers::MAX);
}

namespace
{
  const char* HEALTH = R"(
#include "script/userScript.h"
namespace P64::Script::C0FFEE0000000001
{
  P64_CONDITION_DATA(
    [[P64::Name("Compare"), P64::Enum("0=Less, 1=Greater")]]
    uint8_t compare = 0;
    uint8_t pad0;
    uint16_t pad1;
    [[P64::Name("Value")]]
    float value = 25.0f;
  );

  // P64_CONDITION_TRIGGERS("comment::Ignored"_event);
  P64_CONDITION_TRIGGERS("player::HealthChanged"_event, "scene::Loaded"_event);

  bool evaluate(Object& obj, Data *data) { return true; }
}
)";

  Utils::CPP::Condition parse(const std::string &code)
  {
    return Utils::CPP::parseCondition(code, Utils::CPP::parseEventPins(code));
  }

  bool hasError(const Utils::CPP::Condition &condition, const std::string &part)
  {
    for(const auto &err : condition.errors) {
      if(err.find(part) != std::string::npos) return true;
    }
    return false;
  }
}

TEST_CASE("condition script: P64_CONDITION_DATA and its triggers")
{
  auto condition = parse(HEALTH);
  CHECK(condition.isCondition);
  CHECK(condition.errors.empty());
  REQUIRE(condition.triggers.size() == 2);
  CHECK(condition.triggers[0] == "player::HealthChanged");
  CHECK(condition.triggers[1] == "scene::Loaded");
  CHECK(condition.inputs.empty());
}

TEST_CASE("condition script: an ordinary script is not a condition")
{
  auto condition = parse("P64_DATA(float speed;);\nP64_EVENTS_IN(Open);\nbool evaluate(Object& obj, Data *data) { return true; }");
  CHECK_FALSE(condition.isCondition);
  CHECK(condition.errors.empty());

  auto misplaced = parse("P64_DATA(float speed;);\nP64_CONDITION_TRIGGERS(\"scene::Loaded\"_event);");
  CHECK_FALSE(misplaced.isCondition);
  CHECK(hasError(misplaced, "only works in a condition script"));
}

TEST_CASE("condition script: inputs that read conditions must be input pins")
{
  auto logic = parse(R"(
    P64_CONDITION_DATA();
    P64_EVENTS_IN(Inputs, Other);
    P64_CONDITION_INPUTS(Inputs);
    void onInput(Object& obj, Data *data, In input, const Events::Event& ev) {}
    bool evaluate(Object& obj, Data *data) { return true; }
  )");
  CHECK(logic.isCondition);
  CHECK(logic.errors.empty());
  REQUIRE(logic.inputs.size() == 1);
  CHECK(logic.readsConditions("Inputs"));
  CHECK_FALSE(logic.readsConditions("Other"));

  auto wrong = parse("P64_CONDITION_DATA();\nP64_EVENTS_IN(A);\nP64_CONDITION_INPUTS(B);\nbool evaluate(Object& obj, Data *data) { return true; }");
  CHECK(wrong.inputs.empty());
  CHECK(hasError(wrong, "'B' is not an input pin"));
}

TEST_CASE("condition script: problems are reported")
{
  CHECK(hasError(parse("P64_CONDITION_DATA();"), "bool evaluate"));
  CHECK(hasError(parse("P64_CONDITION_DATA();\nP64_DATA(int a;);\nbool evaluate(Object& obj, Data *data) { return true; }"),
    "replaces P64_DATA"));
  CHECK(hasError(parse("P64_CONDITION_DATA();\nP64_CONDITION_TRIGGERS(scene::Loaded);\nbool evaluate(Object& obj, Data *data) { return true; }"),
    "write each trigger as"));
  CHECK(hasError(parse("P64_CONDITION_DATA();\nP64_EVENTS_IN(Reset);\nbool evaluate(Object& obj, Data *data) { return true; }"),
    "input pin 'Reset' is one every condition has"));
  CHECK(hasError(parse("P64_CONDITION_DATA();\nP64_EVENTS_OUT(Result);\nbool evaluate(Object& obj, Data *data) { return true; }"),
    "output pin 'Result' is one every condition has"));

  // an empty list is fine
  auto empty = parse("P64_CONDITION_DATA();\nP64_CONDITION_TRIGGERS();\nbool evaluate(Object& obj, Data *data) { return true; }");
  CHECK(empty.errors.empty());
  CHECK(empty.triggers.empty());
}

// ---- the project index (what the build checks)

#include "project/events/eventRules.h"

namespace
{
  using namespace Project::Events;

  bool hasMessage(const std::vector<Message> &messages, Message::Level level, const std::string &part)
  {
    for(const auto &msg : messages) {
      if(msg.level == level && msg.text.find(part) != std::string::npos) return true;
    }
    return false;
  }
}

TEST_CASE("index: an input that reads conditions needs a condition's Result behind the name")
{
  Index index{};
  index.add("scene::Low.Result", UseKind::BoundOut, "Low / HealthLow.Result");
  index.conditionResults.insert("scene::Low.Result");

  index.add("scene::Low.Result", UseKind::BoundIn, "And / ConditionAnd.Inputs");
  index.conditionReads.emplace_back("scene::Low.Result", "And / ConditionAnd.Inputs");

  // bound to an ordinary event: it fires, but it is no condition
  index.add("scene::Plate.Pressed", UseKind::BoundOut, "Plate / Plate.Pressed");
  index.add("scene::Plate.Pressed", UseKind::BoundIn, "Or / ConditionOr.Inputs");
  index.conditionReads.emplace_back("scene::Plate.Pressed", "Or / ConditionOr.Inputs");

  // bound to a name nothing uses: one message, not two
  index.add("scene::Missing.Result", UseKind::BoundIn, "Not / ConditionNot.Inputs");
  index.conditionReads.emplace_back("scene::Missing.Result", "Not / ConditionNot.Inputs");

  auto msgs = index.check(Catalog{});
  CHECK_FALSE(hasMessage(msgs, Message::Level::Warning, "And / ConditionAnd.Inputs reads"));
  CHECK(hasMessage(msgs, Message::Level::Warning,
    "Or / ConditionOr.Inputs reads 'scene::Plate.Pressed', but no condition's Result is bound to it; it counts as false"));
  CHECK(hasMessage(msgs, Message::Level::Warning, "Not / ConditionNot.Inputs reads 'scene::Missing.Result'"));
  CHECK_FALSE(hasMessage(msgs, Message::Level::Warning, "Not / ConditionNot.Inputs listens to"));
}

TEST_CASE("index: prefab_0:: and prefab:: are the same Result name")
{
  Index index{};
  index.conditionResults.insert(canonicalName("prefab_0::Cond.Result"));
  index.add("prefab::Cond.Result", UseKind::BoundOut, "Cond / Kit.Result");
  index.add("prefab::Cond.Result", UseKind::BoundIn, "Not / ConditionNot.Inputs");
  index.conditionReads.emplace_back("prefab::Cond.Result", "Not / ConditionNot.Inputs");
  CHECK_FALSE(hasMessage(index.check(Catalog{}), Message::Level::Warning, "reads"));
}

TEST_CASE("source scan: trigger names are marked, and the index leaves them to the build")
{
  auto found = scanSource(
    "P64_CONDITION_TRIGGERS(\"player::HealthChanged\"_event, \"scene::Loaded\"_event);\n"
    "void f() { P64::Events::post(\"player::Healed\"_event); send(o, \"Check\"_pin); }\n", "Cond.cpp");
  REQUIRE(found.size() == 4);
  CHECK(found[0].name == "player::HealthChanged");
  CHECK(found[0].trigger);
  CHECK(found[1].name == "scene::Loaded");
  CHECK(found[1].trigger);
  CHECK(found[2].name == "player::Healed");
  CHECK_FALSE(found[2].trigger);
  CHECK(found[2].line == 2);
  CHECK(found[3].name == "Check");
  CHECK(found[3].isPin);

  // counted as code, a trigger would hide "nothing fires it": the build's binding on Check says "listens"
  Index index{};
  index.addSourceUses(found);
  REQUIRE(index.uses.size() == 1);
  CHECK(index.uses[0].name == "player::Healed");
  index.add("player::HealthChanged", UseKind::BoundIn, "Low / HealthLow, trigger");
  auto cat = Catalog{};
  cat.namespaces["player"] = "";
  CHECK(hasMessage(index.check(cat), Message::Level::Warning, "listens to 'player::HealthChanged', but nothing fires it"));
}

TEST_CASE("node graphs: the events their Fire Event nodes name")
{
  const char* graph = R"({"groups": [], "links": [], "nodes": [
    {"pos": [0, 0], "typeId": "core.start", "uuid": 1},
    {"pos": [10, 0], "typeId": "core.fireEvent", "uuid": 2, "eventName": "player::HealthChanged", "eventValue": "0"},
    {"pos": [20, 0], "typeId": "core.objEvent", "uuid": 3, "eventType": 4, "eventValue": "1"},
    {"pos": [30, 0], "typeId": "core.fireEvent", "uuid": 4, "eventName": "bad name"}
  ]})";
  auto found = scanNodeGraph(graph, "Heal.p64graph");
  REQUIRE(found.size() == 2);
  CHECK(found[0].name == "player::HealthChanged");
  CHECK(found[0].file == "Heal.p64graph");
  CHECK_FALSE(found[0].isPin);
  CHECK(found[1].name == "bad name");

  CHECK(scanNodeGraph("not json", "x.p64graph").empty());
  CHECK(scanNodeGraph("{\"nodes\": 3}", "x.p64graph").empty());
}

TEST_CASE("index: a Fire Event node fires, so a trigger bound to its event is not reported")
{
  auto cat = Catalog{};
  cat.namespaces["player"] = "";
  Index index{};
  index.add("player::HealthChanged", UseKind::GraphOut, "node graph Heal.p64graph (Fire Event)");
  index.add("player::HealthChanged", UseKind::BoundIn, "Low / HealthLow, trigger");
  index.add("bad name", UseKind::GraphOut, "node graph Heal.p64graph (Fire Event)");
  auto msgs = index.check(cat);
  CHECK_FALSE(hasMessage(msgs, Message::Level::Warning, "nothing fires it"));
  // the name becomes a "..."_event literal in generated code: a bad one cannot compile
  CHECK(hasMessage(msgs, Message::Level::Error, "node graph Heal.p64graph (Fire Event): "));

  // alone, it fires an event nobody listens to
  Index lonely{};
  lonely.add("player::HealthChanged", UseKind::GraphOut, "node graph Heal.p64graph (Fire Event)");
  CHECK_FALSE(hasMessage(lonely.check(cat), Message::Level::Warning, "nothing fires it"));
}

TEST_CASE("index: an event nothing listens to is noted once, however many outputs fire it")
{
  auto cat = Catalog{};
  cat.namespaces["player"] = "";
  Index index{};
  index.add("player::HealthChanged", UseKind::BoundOut, "scene 1 / Player / Damagable.HealthChanged");
  index.add("player::HealthChanged", UseKind::BoundOut, "scene 2 / Player / Damagable.HealthChanged");
  index.add("player::HealthChanged", UseKind::BoundOut, "prefab Player.prefab / Player / Damagable.HealthChanged");
  index.add("scene::Lever.Pulled", UseKind::BoundOut, "lever.Pulled");

  auto msgs = index.check(cat);
  int notes = 0;
  for(const auto &msg : msgs) {
    if(msg.level == Message::Level::Info && msg.text.find("player::HealthChanged") != std::string::npos) ++notes;
  }
  CHECK(notes == 1);
  CHECK(hasMessage(msgs, Message::Level::Info,
    "scene 1 / Player / Damagable.HealthChanged fires 'player::HealthChanged', but nothing listens to it (and 2 more outputs)"));
  CHECK(hasMessage(msgs, Message::Level::Info, "lever.Pulled fires 'scene::Lever.Pulled', but nothing listens to it"));
  CHECK_FALSE(hasMessage(msgs, Message::Level::Info, "lever.Pulled fires 'scene::Lever.Pulled', but nothing listens to it ("));
}

