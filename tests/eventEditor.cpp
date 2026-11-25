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
#include "project/events/eventPins.h"
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

TEST_SUITE("prefab scope")
{
  // the scene, then prefab instance P, then prefab instance Q inside it, the object inside Q
  std::vector<ScopeLevel> sceneLevels()
  {
    std::vector<ScopeLevel> levels(3);
    levels[0].isScene = true;
    levels[1].skip = 1; // Q's root sits between the object and P's root
    levels[2].skip = 0;
    return levels;
  }

  TEST_CASE("numbers shift") {
    CHECK(shiftPrefabNumber("prefab::X", 1) == "prefab_1::X");
    CHECK(shiftPrefabNumber("prefab_1::X", 1) == "prefab_2::X");
    CHECK(shiftPrefabNumber("prefab_2::X", -1) == "prefab_1::X");
    CHECK(shiftPrefabNumber("prefab_1::X", -1) == "prefab::X");
    CHECK(shiftPrefabNumber("prefab_0::X", 2) == "prefab_2::X");
    CHECK(shiftPrefabNumber("scene::X", 1) == "scene::X");
  }

  TEST_CASE("prefab:: is the graph's own instance, prefab_1:: the one around it") {
    auto levels = sceneLevels();
    auto own = resolveName("prefab::Alarm", levels, 2);
    CHECK(own.problem.empty());
    CHECK(own.scope == Scope::Prefab);
    CHECK(own.up == 0);
    CHECK(own.hash == ("prefab::Alarm"_event).hash);

    auto parent = resolveName("prefab_1::Alarm", levels, 2);
    CHECK(parent.up == 1);
    CHECK(parent.hash == own.hash);

    // written in P's graph: P is reached by skipping Q's root
    CHECK(resolveName("prefab::Alarm", levels, 1).up == 1);
  }

  TEST_CASE("past the outermost prefab of a scene, a name reaches nothing") {
    auto levels = sceneLevels();
    auto res = resolveName("prefab_2::Alarm", levels, 2);
    CHECK(res.problem.empty());
    CHECK_FALSE(res.warning.empty());
  }

  TEST_CASE("prefab:: is not allowed in the scene's own graph") {
    auto levels = sceneLevels();
    CHECK_FALSE(resolveName("prefab::Alarm", levels, 0).problem.empty());
    CHECK(resolveName("scene::Alarm", levels, 0).problem.empty());
  }

  TEST_CASE("a repointed link sends the name somewhere else") {
    auto levels = sceneLevels();
    levels[2].links.push_back({"prefab_1::Alarm", "scene::Siren"});
    auto res = resolveName("prefab_1::Alarm", levels, 2);
    CHECK(res.scope == Scope::Scene);
    CHECK(res.hash == ("scene::Siren"_event).hash);

    levels[2].links = {{"prefab_1::Alarm", "prefab::Bell"}};
    res = resolveName("prefab_1::Alarm", levels, 2);
    CHECK(res.scope == Scope::Prefab);
    CHECK(res.up == 1);
    CHECK(res.hash == ("prefab::Bell"_event).hash);
  }

  TEST_CASE("a link only applies to the name it was made for") {
    auto levels = sceneLevels();
    levels[2].links.push_back({"prefab_1::Other", "scene::Siren"});
    CHECK(resolveName("prefab_1::Alarm", levels, 2).scope == Scope::Prefab);
  }

  TEST_CASE("above a prefab built for spawning, the runtime keeps climbing") {
    std::vector<ScopeLevel> levels(2);
    levels[0].spawnedRoot = true;
    levels[0].skip = 1;
    auto res = resolveName("prefab_2::Alarm", levels, 1);
    CHECK(res.problem.empty());
    CHECK(res.warning.empty());
    CHECK(res.up == 2); // past Q's root (skip) and one more above the spawned root
  }
}

TEST_SUITE("moving content across a prefab boundary")
{
  TEST_CASE("To Prefab: scene names only the moved objects use become the prefab's own") {
    std::set<std::string> local{"scene::Plate.Pressed"};
    CHECK(nameAfterWrap("scene::Plate.Pressed", local) == "prefab::Plate.Pressed");
    CHECK(nameAfterWrap("scene::Shared", local) == "scene::Shared");
    CHECK(nameAfterWrap("global::Engine.SceneLoaded", local) == "global::Engine.SceneLoaded");
    CHECK(nameAfterWrap("prefab_1::Alarm", local) == "prefab_1::Alarm");
    CHECK(nameAfterWrap("object::Hit", local) == "object::Hit");
  }

  TEST_CASE("Unpack: the prefab's own names become scene names") {
    UnpackRules rules{};
    rules.local["prefab::Plate.Pressed"] = "scene::K1.Plate.Pressed";
    CHECK(nameAfterUnpack("prefab::Plate.Pressed", rules) == "scene::K1.Plate.Pressed");
    CHECK(nameAfterUnpack("prefab_0::Plate.Pressed", rules) == "scene::K1.Plate.Pressed"); // the same event
    CHECK(nameAfterUnpack("prefab::Other", rules) == "scene::Other"); // not in the table: the plain scene name
    CHECK(nameAfterUnpack("scene::Lever", rules) == "scene::Lever");
    CHECK(nameAfterUnpack("ui::Hud.Shown", rules) == "ui::Hud.Shown");
  }

  TEST_CASE("Unpack: numbered names follow the instance's links one graph out") {
    UnpackRules rules{};
    rules.links.push_back({"prefab_1::Alarm", "scene::GlobalAlarm"});
    CHECK(nameAfterUnpack("prefab_1::Alarm", rules) == "scene::GlobalAlarm");
    // not repointed: prefab:: in the scene's own graph, which reached nothing before either
    CHECK(nameAfterUnpack("prefab_1::Bell", rules).empty());
    CHECK(nameAfterUnpack("prefab_2::Bell", rules).empty());
    rules.links.push_back({"prefab_2::Far", "prefab_1::Far"});
    CHECK(nameAfterUnpack("prefab_2::Far", rules).empty());
  }

  TEST_CASE("Unpack: a clashing scene name gets the instance's name") {
    std::set<std::string> taken{"scene::Opened", "scene::Kit.Opened"};
    auto isTaken = [&](const std::string &n) { return taken.contains(n); };
    CHECK(freeSceneName("prefab::Pressed", "Kit", isTaken) == "scene::Pressed");
    CHECK(freeSceneName("prefab::Opened", "Kit", isTaken) == "scene::Kit_2.Opened");
    CHECK(freeSceneName("prefab::Opened", "Door (1)", isTaken) == "scene::Door1.Opened");
    CHECK(freeSceneName("prefab::Opened", "  ", isTaken) == "scene::Instance.Opened");
    CHECK(freeSceneName("prefab::Spawned", "Kit", isTaken, true) == "scene::Kit.Spawned");
    CHECK(nameProblem(freeSceneName("prefab::A.B", "x-y z", isTaken, true)).empty());
  }

  TEST_CASE("Unpack: life-cycle events are fired by the root's own pins") {
    CHECK(lifecyclePin("prefab::Spawned") == "Ready");
    CHECK(lifecyclePin("prefab_0::Enabled") == "Enabled");
    CHECK(lifecyclePin("prefab::Disabled") == "Disabled");
    CHECK(lifecyclePin("prefab::Destroying") == "Destroyed");
    CHECK(lifecyclePin("prefab::Opened").empty());
    CHECK(lifecyclePin("prefab_1::Spawned").empty()); // the parent's: an ordinary parent link
  }
}

TEST_SUITE("prefab scope")
{
  TEST_CASE("the resolved name is the one a repointed link ends up as") {
    std::vector<ScopeLevel> levels(3);
    levels[0].isScene = true;
    levels[2].links.push_back({"prefab_1::Alarm", "scene::Alarm7"});
    CHECK(resolveName("prefab_1::Alarm", levels, 2).name == "scene::Alarm7");
    CHECK(resolveName("prefab::Plate.Pressed", levels, 2).name == "prefab::Plate.Pressed");
    CHECK(resolveName("scene::Lever", levels, 2).name == "scene::Lever");
  }
}

// Fork: animation events (docs/todo/anim-events/PLAN.md)
TEST_SUITE("animation events")
{
  const char* LIST = R"({
    "animationEvents": {
      "assets/hero.glb": {
        "doc": "the hero",
        "events": [
          {"name": "Run.STEP_LEFT", "doc": "Run, at 0.65 s", "value": "the clip index"},
          {"name": "Leap.LAND"},
          {"name": "Any.LAND", "doc": "LAND of any clip"}
        ]
      }
    }
  })";

  TEST_CASE("the catalog lists a model's events in file order") {
    TempDir dir{};
    dir.write("events/anim.json", LIST);
    Catalog cat{};
    std::vector<std::string> errors{};
    cat.loadDir(dir.path / "events", false, errors);
    CHECK(errors.empty());
    const auto *list = cat.findAnimEvents("assets/hero.glb");
    REQUIRE(list != nullptr);
    CHECK(list->doc == "the hero");
    REQUIRE(list->events.size() == 3);
    CHECK(list->events[0].name == "Run.STEP_LEFT");
    CHECK(list->events[0].value == "the clip index");
    CHECK(list->events[2].name == "Any.LAND");
    CHECK(cat.findAnimEvents("assets/other.glb") == nullptr);
  }

  TEST_CASE("bad names, duplicates, a model listed twice and too many events are errors") {
    TempDir dir{};
    dir.write("events/a.json", R"({"animationEvents": {"assets/a.glb": {"events": [
      {"name": "Run..STEP"}, {"name": "Run.STEP-1"}, {"name": "Ok"}, {"name": "Ok"}, {"name": "MainFinished"}, {"doc": "no name"}
    ]}}})");
    dir.write("events/b.json", R"({"animationEvents": {"assets/a.glb": {"events": []}, "assets/c.glb": {"events": "x"}}})");
    std::string many = R"({"animationEvents": {"assets/many.glb": {"events": [)";
    for(int i = 0; i < 70; ++i) many += (i ? "," : "") + std::string{"{\"name\": \"E"} + std::to_string(i) + "\"}";
    many += "]}}}";
    dir.write("events/c.json", many);

    Catalog cat{};
    std::vector<std::string> errors{};
    cat.loadDir(dir.path / "events", false, errors);
    auto has = [&](const std::string &part) {
      return std::any_of(errors.begin(), errors.end(), [&](const std::string &e) { return e.find(part) != std::string::npos; });
    };
    CHECK(has("'Run..STEP' has an empty part"));
    CHECK(has("'Run.STEP-1' is not a valid animation event name"));
    CHECK(has("'Ok' is listed twice"));
    CHECK(has("'MainFinished' is a pin Model (Animated) already has"));
    CHECK(has("every event needs a \"name\""));
    CHECK(has("the model is listed twice"));
    CHECK(has("needs an \"events\" array"));
    CHECK(has("70 events, at most 64"));
    REQUIRE(cat.findAnimEvents("assets/a.glb") != nullptr);
    CHECK(cat.findAnimEvents("assets/a.glb")->events.size() == 1); // only "Ok"
    CHECK(cat.findAnimEvents("assets/many.glb")->events.size() == P64::Events::Pins::ANIM_EVENTS_MAX);
  }

  TEST_CASE("the pins follow the component's own outputs, from ANIM_EVENT_PIN_BASE") {
    TempDir dir{};
    dir.write("events/anim.json", LIST);
    Catalog cat{};
    std::vector<std::string> errors{};
    cat.loadDir(dir.path / "events", false, errors);

    PinList pins{};
    pins.in = {"PlayMain", "PlayBlend"};
    pins.out = {"MainFinished"};
    pins.addAnimEvents(*cat.findAnimEvents("assets/hero.glb"));
    REQUIRE(pins.out.size() == 4);
    CHECK(pins.indexOf(false, "MainFinished") == 0);
    CHECK(pins.indexOf(false, "Run.STEP_LEFT") == P64::Events::Pins::ANIM_EVENT_PIN_BASE);
    CHECK(pins.indexOf(false, "Any.LAND") == P64::Events::Pins::ANIM_EVENT_PIN_BASE + 2);
    CHECK(pins.indexOf(true, "Run.STEP_LEFT") == -1);
    CHECK(pins.indexOf(true, "PlayBlend") == 1);
    // the same index range as condition pins, but not a condition
    CHECK_FALSE(pins.isConditionPin(false, "Run.STEP_LEFT"));
    CHECK(pins.docOf("Run.STEP_LEFT") == "Run, at 0.65 s\nValue: the clip index");
    CHECK(pins.docOf("Leap.LAND").empty());

    PinList none{};
    none.out = {"MainFinished"};
    none.addAnimEvents({});
    CHECK(none.animEventOut == -1);
    CHECK(none.out.size() == 1);
  }

  TEST_CASE("a dotted pin name hashes like the editor and Python's zlib.crc32") {
    CHECK(P64::Events::pinHash("Run.STEP_LEFT", 13) == 0xDE86E37Du);
    CHECK(P64::Events::pinHash("Any.LAND", 8) == 0xB69100BEu);
  }
}
