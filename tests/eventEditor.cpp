/**
* @license MIT
*/
// Fork: event graph, editor side: pin declarations in scripts, binding files, name rules, the
// catalog, the source scan and the project-wide checks.
#include "doctest/doctest.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "utils/codeParser.h"
#include "project/events/eventBindings.h"
#include "project/events/eventRules.h"
#include "events/pins.h"

namespace fs = std::filesystem;
using namespace Project::Events;
using Level = Message::Level;

namespace
{
  bool hasMessage(const std::vector<Message> &messages, Level level, const std::string &part)
  {
    for(const auto &msg : messages) {
      if(msg.level == level && msg.text.find(part) != std::string::npos) return true;
    }
    return false;
  }

  struct TempDir
  {
    fs::path path;
    TempDir() {
      path = fs::temp_directory_path() / ("p64_event_test_" + std::to_string(std::rand()));
      fs::create_directories(path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    void write(const std::string &name, const std::string &text) const {
      fs::create_directories((path / name).parent_path());
      std::ofstream(path / name) << text;
    }
  };

  Catalog engineCatalog()
  {
    Catalog cat{};
    cat.events["global::Engine.SceneLoaded"] = {"", "", true};
    cat.events["scene::Loaded"] = {"", "", true};
    return cat;
  }
}

TEST_SUITE("event pins in scripts")
{
  TEST_CASE("object script pins") {
    auto pins = Utils::CPP::parseEventPins(R"(
      namespace P64::Script::C1234567890ABCDEF {
        P64_DATA( float t; );
        P64_EVENTS_IN(Open, Close);
        P64_EVENTS_OUT( Opened ,Closed );
      })");
    CHECK(pins.in == std::vector<std::string>{"Open", "Close"});
    CHECK(pins.out == std::vector<std::string>{"Opened", "Closed"});
    CHECK(pins.errors.empty());
  }

  TEST_CASE("comments, definitions and longer names are ignored") {
    auto pins = Utils::CPP::parseEventPins(R"(
      // P64_EVENTS_IN(Commented);
      /* P64_EVENTS_OUT(Block); */
      #define P64_EVENTS_IN(...) enum class In : uint8_t { __VA_ARGS__ };
      MY_P64_EVENTS_IN(Other);
      P64_EVENTS_IN(Real);
    )");
    CHECK(pins.in == std::vector<std::string>{"Real"});
    CHECK(pins.out.empty());
  }

  TEST_CASE("bad pin names and duplicates are reported") {
    auto pins = Utils::CPP::parseEventPins("P64_EVENTS_IN(Open, 2nd, Open, Ok_1);");
    CHECK(pins.in == std::vector<std::string>{"Open", "Ok_1"});
    CHECK(pins.errors.size() == 2);
  }

  TEST_CASE("a script without pins has none") {
    auto pins = Utils::CPP::parseEventPins("void update(Object& obj, Data* data, float dt) {}");
    CHECK(pins.empty());
    CHECK(pins.errors.empty());
  }

  TEST_CASE("global script systems") {
    std::vector<std::string> errors{};
    auto systems = Utils::CPP::parseGlobalEventPins(R"(
      P64_GLOBAL_EVENTS_IN(Save, Requested, Clear);
      P64_GLOBAL_EVENTS_OUT(LevelDriver, TransitionStarted, TransitionFinished);
      P64_GLOBAL_EVENTS_OUT(Save, Written);
    )", &errors);
    REQUIRE(systems.size() == 2);
    CHECK(systems[0].system == "Save");
    CHECK(systems[0].in == std::vector<std::string>{"Requested", "Clear"});
    CHECK(systems[0].out == std::vector<std::string>{"Written"});
    CHECK(systems[1].system == "LevelDriver");
    CHECK(systems[1].out == std::vector<std::string>{"TransitionStarted", "TransitionFinished"});
    CHECK(errors.empty());
  }

  TEST_CASE("a system declared twice in one direction is reported") {
    std::vector<std::string> errors{};
    auto systems = Utils::CPP::parseGlobalEventPins("P64_GLOBAL_EVENTS_IN(Save, A); P64_GLOBAL_EVENTS_IN(Save, B);", &errors);
    REQUIRE(systems.size() == 1);
    CHECK(systems[0].in == std::vector<std::string>{"A"});
    CHECK(errors.size() == 1);
  }
}

TEST_SUITE("event binding files")
{
  TEST_CASE("bindings round trip through JSON, sorted and without empty entries") {
    Bindings b{};
    b.add(true, "Open", "scene::Trigger.Enter");
    b.add(true, "Open", "scene::Trigger.Enter"); // not twice
    b.add(true, "Close", "scene::Trigger.Exit");
    b.add(false, "Opened", "global::Doors.Opened");
    b.in["Empty"] = {};

    auto json = b.toJson();
    CHECK(json.dump() == R"({"in":{"Close":["scene::Trigger.Exit"],"Open":["scene::Trigger.Enter"]},"out":{"Opened":["global::Doors.Opened"]}})");

    auto back = Bindings::fromJson(json);
    CHECK(back.count() == 3);
    CHECK(back.in.at("Open") == std::vector<std::string>{"scene::Trigger.Enter"});
    CHECK_FALSE(back.in.contains("Empty"));
  }

  TEST_CASE("a single string and the object form are read too") {
    auto b = Bindings::fromJson(nlohmann::json::parse(R"({"in":{"A":"scene::X","B":[{"event":"scene::Y"}, ""]}})"));
    CHECK(b.in.at("A") == std::vector<std::string>{"scene::X"});
    CHECK(b.in.at("B") == std::vector<std::string>{"scene::Y"});
  }

  TEST_CASE("no bindings write nothing") {
    Bindings b{};
    CHECK(b.empty());
    CHECK(b.toJson().empty());
  }
}

TEST_SUITE("event rules")
{
  TEST_CASE("name problems are explained") {
    CHECK(nameProblem("scene::Loaded").empty());
    CHECK(nameProblem("Loaded").find("namespace") != std::string::npos);
    CHECK(nameProblem("global::Save").find("System.Pin") != std::string::npos);
    CHECK_FALSE(nameProblem("scene::a b").empty());
  }

  TEST_CASE("namespaces") {
    CHECK(namespaceOf("scene::Loaded") == "scene");
    CHECK(namespaceOf("Loaded").empty());
    CHECK(isBuiltinNamespace("prefab_3"));
    CHECK(isBuiltinNamespace("ui"));
    CHECK_FALSE(isBuiltinNamespace("prefab_x"));
    CHECK_FALSE(isBuiltinNamespace("mission"));
    CHECK(canonicalName("prefab_2::Alarm") == "prefab::Alarm");
    CHECK(canonicalName("scene::Alarm") == "scene::Alarm");
  }

  TEST_CASE("record kind packs direction and scope like the runtime reads it") {
    CHECK(recordKind(true, Scope::Global) == 0x01);
    CHECK(recordKind(false, Scope::Scene) == 0x02);
    CHECK(recordKind(true, Scope::Object) == (1 | (3 << 1)));
  }

  TEST_CASE("catalog files") {
    TempDir dir{};
    dir.write("events/game.json", R"({
      "namespaces": { "mission": { "doc": "Missions" }, "Bad Ns": {}, "scene": {} },
      "events": { "mission::Started": { "doc": "A mission began", "value": "id" }, "nope": {} }
    })");
    dir.write("events/notes.txt", "ignored");
    Catalog cat{};
    std::vector<std::string> errors{};
    cat.loadDir(dir.path / "events", false, errors);
    CHECK(cat.isDeclaredNamespace("mission"));
    CHECK(cat.isDeclaredNamespace("scene"));
    CHECK_FALSE(cat.isDeclaredNamespace("quest"));
    REQUIRE(cat.find("mission::Started") != nullptr);
    CHECK(cat.find("mission::Started")->doc == "A mission began");
    CHECK_FALSE(cat.find("mission::Started")->engine);
    CHECK(errors.size() == 2); // the bad namespace and the bad event name
  }

  TEST_CASE("the source scan finds event and pin literals with their lines") {
    auto found = scanSource("int a;\nP64::Events::post(\"mission::Started\"_event);\nsend(*e, \"TakeDamage\"_pin);\nauto s = \"not_an_event\";\n", "x.cpp");
    REQUIRE(found.size() == 2);
    CHECK(found[0].name == "mission::Started");
    CHECK(found[0].line == 2);
    CHECK_FALSE(found[0].isPin);
    CHECK(found[1].name == "TakeDamage");
    CHECK(found[1].isPin);
    CHECK(found[1].line == 3);
  }

  TEST_CASE("systems: duplicates and the reserved Engine name") {
    Index index{};
    std::vector<Message> msgs{};
    index.addSystem({"Save", "a.cpp", {"Requested"}, {"Written"}}, msgs);
    index.addSystem({"Save", "b.cpp", {}, {}}, msgs);
    index.addSystem({"Engine", "c.cpp", {}, {}}, msgs);
    index.addSystem({"Both", "d.cpp", {"X"}, {"X"}}, msgs);
    CHECK(hasMessage(msgs, Level::Error, "declared by both a.cpp and b.cpp"));
    CHECK(hasMessage(msgs, Level::Error, "reserved"));
    CHECK(hasMessage(msgs, Level::Error, "both input and output"));
  }

  TEST_CASE("global bindings must face a system pin the other way") {
    Index index{};
    std::vector<Message> msgs{};
    index.addSystem({"Save", "save.cpp", {"Requested"}, {"Written"}}, msgs);
    auto cat = engineCatalog();

    CHECK(bindingProblem(index, cat, "global::Save.Written", true).empty());    // listen to its output
    CHECK(bindingProblem(index, cat, "global::Save.Requested", false).empty()); // fire into its input
    CHECK_FALSE(bindingProblem(index, cat, "global::Save.Written", false).empty());
    CHECK_FALSE(bindingProblem(index, cat, "global::Save.Requested", true).empty());
    CHECK_FALSE(bindingProblem(index, cat, "global::Save.Missing", true).empty());
    CHECK_FALSE(bindingProblem(index, cat, "global::Nobody.X", true).empty());
    CHECK(bindingProblem(index, cat, "global::Engine.SceneLoaded", true).empty());
    CHECK_FALSE(bindingProblem(index, cat, "global::Engine.SceneLoaded", false).empty());
    CHECK_FALSE(bindingProblem(index, cat, "global::Engine.Nope", true).empty());
    CHECK(bindingProblem(index, cat, "scene::Anything", true).empty());
  }

  TEST_CASE("project checks") {
    auto cat = engineCatalog();
    cat.namespaces["mission"] = "";
    Index index{};
    index.addCatalog(cat);
    index.add("scene::Door.Open", UseKind::BoundIn, "door.Open");             // nothing fires it
    index.add("scene::Lever.Pulled", UseKind::BoundOut, "lever.Pulled");      // nobody listens
    index.add("scene::Loaded", UseKind::BoundIn, "x.In");                     // the engine fires it
    index.add("quest::Started", UseKind::BoundIn, "q.In");                    // undeclared namespace
    index.add("mission::Begun", UseKind::Code, "m.cpp:3");                    // declared, code
    index.add("scene::Alarm", UseKind::BoundOut, "a.Out");
    index.add("scene::alarm", UseKind::BoundIn, "b.In");                      // differs by case
    index.add("scene::Alarms", UseKind::BoundIn, "c.In");                     // differs by one character
    index.sourcePins.push_back({"TakeDamage", "e.cpp", 9, true});
    index.declaredInputPins.insert("Heal");

    auto msgs = index.check(cat);
    CHECK(hasMessage(msgs, Level::Warning, "door.Open listens to 'scene::Door.Open', but nothing fires it"));
    CHECK(hasMessage(msgs, Level::Info, "lever.Pulled fires 'scene::Lever.Pulled', but nothing listens"));
    CHECK_FALSE(hasMessage(msgs, Level::Warning, "x.In listens"));
    CHECK(hasMessage(msgs, Level::Error, "namespace 'quest' is not declared"));
    CHECK_FALSE(hasMessage(msgs, Level::Error, "namespace 'mission'"));
    CHECK(hasMessage(msgs, Level::Warning, "differ only by case"));
    CHECK(hasMessage(msgs, Level::Warning, "differ by one character"));
    CHECK(hasMessage(msgs, Level::Warning, "\"TakeDamage\"_pin"));
  }

  TEST_CASE("similar names used both ways are not reported as typos") {
    Index index{};
    index.add("scene::Wave1", UseKind::BoundOut, "a");
    index.add("scene::Wave1", UseKind::BoundIn, "b");
    index.add("scene::Wave2", UseKind::BoundOut, "c");
    index.add("scene::Wave2", UseKind::BoundIn, "d");
    auto msgs = index.check(Catalog{});
    CHECK_FALSE(hasMessage(msgs, Level::Warning, "one character"));
  }

  TEST_CASE("names for the debug table are canonical and unique") {
    Index index{};
    index.add("prefab_1::Alarm", UseKind::BoundIn, "a");
    index.add("prefab::Alarm", UseKind::BoundOut, "b");
    index.add("scene::Loaded", UseKind::EngineOut, "engine");
    index.add("bad name", UseKind::Code, "c");
    CHECK(index.allNames() == std::vector<std::string>{"prefab::Alarm", "scene::Loaded"});
  }
}

TEST_SUITE("engine pins")
{
  TEST_CASE("every object has the same pins, in a fixed order") {
    using namespace P64::Events::Pins;
    CHECK(OBJECT_IN_LIST.count == 6);
    CHECK(std::string(OBJECT_IN[(int)ObjectIn::Destroy]) == "Destroy");
    CHECK(std::string(OBJECT_OUT[(int)ObjectOut::Ready]) == "Ready");
    CHECK(OBJECT_OUT_LIST.count == 4);
  }

  TEST_CASE("engine components") {
    using namespace P64::Events::Pins;
    const auto *collider = find(5);
    REQUIRE(collider != nullptr);
    CHECK(collider->in.count == 0);
    REQUIRE(collider->out.count == 4);
    CHECK(std::string(collider->out.names[(int)ColliderOut::Emptied]) == "Emptied");
    CHECK(find(4)->out.names == collider->out.names); // collision mesh: the same outputs
    CHECK(std::string(find(6)->in.names[(int)AudioIn::Stop]) == "Stop");
    CHECK(std::string(find(10)->out.names[0]) == "MainFinished");
    CHECK(find(0) == nullptr);  // Code: the script declares its pins
    CHECK(find(11) == nullptr); // Rigid-Body: none yet
  }
}
