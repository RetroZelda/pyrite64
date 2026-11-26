/**
* @license MIT
*/
// Fork: dialog trees (docs/todo/dialog/PLAN.md, section 7). The editor's tree model, its build,
// and the engine's reader of the built data.
#include "doctest/doctest.h"
#include <string>

#include "dialog/dialogData.h"
#include "events/types.h"
#include "text/locId.h"
#include "project/dialog/dialogTree.h"

using namespace Project::Dialog;
namespace D = P64::Dialog;

namespace
{
  // Start -> Branch(Condition) -> true: Line "hi" / false: Line "bye"
  // "hi" -> Choice(Yes: Event -> End 7, No: end; option 1 shown when not (input HasTicket or cond))
  Tree makeTree()
  {
    // add() returns a reference into the node list: keep ids, not references, across adds
    Tree t{};
    const uint32_t start = t.add(NodeType::Start, 0, 0, "dlg.t").id;
    const uint32_t branch = t.add(NodeType::Branch, 0, 0, "dlg.t").id;
    const uint32_t cond = t.add(NodeType::Condition, 0, 0, "dlg.t").id;
    t.find(cond)->script = 42;
    t.find(cond)->args["threshold"] = "3";
    const uint32_t hi = t.add(NodeType::Line, 0, 0, "dlg.t").id;
    t.find(hi)->speaker = "tank";
    t.find(hi)->lang = "paired";
    const uint32_t bye = t.add(NodeType::Line, 0, 0, "dlg.t").id;
    t.find(bye)->timed = true;
    const uint32_t choice = t.add(NodeType::Choice, 0, 0, "dlg.t").id;
    t.find(choice)->options[0].event = "shop::Bought";
    t.find(choice)->options[0].eventValue = 20;
    const uint32_t input = t.add(NodeType::Input, 0, 0, "dlg.t").id;
    t.find(input)->name = "HasTicket";
    const uint32_t logic = t.add(NodeType::Logic, 0, 0, "dlg.t").id;
    t.find(logic)->logic = "not";
    const uint32_t event = t.add(NodeType::Event, 0, 0, "dlg.t").id;
    t.find(event)->event = "prefab::Gate.Open";
    t.find(event)->value = 1;
    const uint32_t end = t.add(NodeType::End, 0, 0, "dlg.t").id;
    t.find(end)->value = 7;
    const uint32_t named = t.add(NodeType::Start, 0, 0, "dlg.t").id;
    t.find(named)->name = "again";

    t.links = {
      {start, "Next", branch, "In"},
      {cond, "Result", branch, "Condition"},
      {branch, "True", hi, "In"},
      {branch, "False", bye, "In"},
      {hi, "Next", choice, "In"},
      {choice, "Option 1", event, "In"},
      {event, "Next", end, "In"},
      {input, "Result", logic, "Inputs"},
      {cond, "Result", logic, "Inputs"},
      {logic, "Result", choice, "Show 1"},
      {named, "Next", bye, "In"},
    };
    return t;
  }

  BuildEnv makeEnv()
  {
    BuildEnv env{};
    env.scriptIndex = [](uint64_t uuid) { return uuid == 42 ? 5 : -1; };
    env.writeArgs = [](uint64_t, const std::map<std::string, std::string> &args, std::vector<uint8_t> &out, std::string &) {
      out = {0, 0, 0, uint8_t(std::stoi(args.at("threshold")))};
      return true;
    };
    env.speakerIndex = [](const std::string &id) { return id == "tank" ? 2 : -1; };
    env.portraitIndex = [](const std::string &, const std::string &) { return -1; };
    env.treeAssetIndex = [](uint64_t) { return -1; };
    return env;
  }
}

TEST_CASE("dialog: a tree round-trips through JSON")
{
  auto t = makeTree();
  const auto text = serialize(t);
  Tree back{};
  std::vector<std::string> errors{};
  REQUIRE(parse(nlohmann::json::parse(text), back, errors));
  CHECK(errors.empty());
  CHECK(back.nodes.size() == t.nodes.size());
  CHECK(back.links.size() == t.links.size());
  CHECK(back.nextId == t.nextId);
  CHECK(serialize(back) == text);
  CHECK(keysOf(back).size() == 4); // two lines, two options
  CHECK(back.find(4)->key == "dlg.t.4");
  CHECK(back.find(6)->options[1].key == "dlg.t.6.2");
  CHECK(back.find(6)->options[1].cancel);
}

TEST_CASE("dialog: pins by node type")
{
  Node choice{.type = NodeType::Choice};
  choice.options.resize(3);
  CHECK(outputsOf(choice).size() == 3);
  CHECK(inputsOf(choice).size() == 7); // In, Show/Enable per option
  Node logic{.type = NodeType::Logic};
  CHECK(inputsOf(logic)[0].many);
  CHECK(outputsOf(logic)[0].kind == PinKind::Bool);
  CHECK(keyPrefixOf("Tank Intro.p64dlg") == "dlg.tank_intro");
}

TEST_CASE("dialog: the build is read back by the engine's reader")
{
  auto t = makeTree();
  std::vector<std::string> errors{}, warnings{};
  auto bytes = build(t, makeEnv(), errors, warnings);
  CHECK(errors.empty());
  CHECK(warnings.empty());

  D::TreeView v{};
  REQUIRE(v.init(bytes.data(), static_cast<uint32_t>(bytes.size())));
  CHECK(v.nodeCount() == 8); // starts, branch, two lines, choice, event, end
  CHECK(v.startCount() == 2);
  CHECK(v.conditionCount() == 1);
  CHECK(v.inputCount() == 1);
  CHECK(v.inputHash(0) == P64::Loc::varHash("HasTicket", 9));

  const uint16_t s = v.start();
  REQUIRE(s != D::NONE);
  CHECK(v.type(s) == D::NodeType::Start);
  const uint16_t b = v.next(s);
  REQUIRE(v.type(b) == D::NodeType::Branch);
  const uint16_t hi = v.next(b);
  const uint16_t bye = v.branchFalse(b);
  CHECK(v.type(hi) == D::NodeType::Line);
  CHECK(v.lineKey(hi) == "dlg.t.4"_localized.hash);
  CHECK(v.lineSpeaker(hi) == 2);
  CHECK(v.lineLang(hi) == D::Lang::Paired);
  CHECK(v.lineSpeaker(bye) == 0xFF);
  CHECK(((v.flags(bye) & D::LINE_TIMED) != 0));
  CHECK(v.next(bye) == D::NONE);
  CHECK(v.start(P64::Loc::varHash("again", 5)) != D::NONE);
  CHECK(v.next(v.start(P64::Loc::varHash("again", 5))) == bye);

  // the branch's condition: condition 0, script 5, values blob
  CHECK(v.conditionScript(0) == 5);
  CHECK(v.conditionDataSize(0) == 4);
  CHECK(static_cast<const uint8_t*>(v.conditionData(0))[3] == 3);
  auto yes = [](uint16_t, void*) { return true; };
  auto no = [](uint16_t, void*) { return false; };
  auto noInput = [](uint8_t, void*) { return false; };
  auto hasTicket = [](uint8_t, void*) { return true; };
  CHECK(D::runExpr(v.expr(v.branchExpr(b)), yes, noInput, nullptr));
  CHECK_FALSE(D::runExpr(v.expr(v.branchExpr(b)), no, noInput, nullptr));

  const uint16_t c = v.next(hi);
  REQUIRE(v.type(c) == D::NodeType::Choice);
  CHECK(v.optionCount(c) == 2);
  CHECK(((v.optionFlags(c, 0) & D::OPTION_DEFAULT) != 0));
  CHECK(((v.optionFlags(c, 0) & D::OPTION_EVENT) != 0));
  CHECK(((v.optionFlags(c, 1) & D::OPTION_CANCEL) != 0));
  CHECK(v.optionEvent(c, 0) == "shop::Bought"_event.hash);
  CHECK(v.optionEventValue(c, 0) == 20);
  CHECK(v.optionNext(c, 1) == D::NONE);
  CHECK(v.optionEnable(c, 0) == D::NO_EXPR);
  // Show 1 = not (HasTicket, condition)
  CHECK(D::runExpr(v.expr(v.optionShow(c, 0)), no, noInput, nullptr));
  CHECK_FALSE(D::runExpr(v.expr(v.optionShow(c, 0)), no, hasTicket, nullptr));
  CHECK_FALSE(D::runExpr(v.expr(v.optionShow(c, 0)), yes, noInput, nullptr));

  const uint16_t e = v.optionNext(c, 0);
  REQUIRE(v.type(e) == D::NodeType::Event);
  CHECK(v.eventHash(e) == "prefab::Gate.Open"_event.hash);
  CHECK(v.eventScope(e) == static_cast<uint8_t>(P64::Events::Scope::Prefab));
  CHECK(v.eventValue(e) == 1);
  CHECK(v.type(v.next(e)) == D::NodeType::End);
  CHECK(v.endValue(v.next(e)) == 7);

  bytes[3] = 'X';
  CHECK_FALSE(v.init(bytes.data(), static_cast<uint32_t>(bytes.size())));
}

TEST_CASE("dialog: build errors and warnings")
{
  auto t = makeTree();
  t.find(4)->speaker = "nobody";
  t.find(9)->event = "not an event";
  t.find(3)->script = 99; // not a condition script
  t.add(NodeType::Line, 0, 0, "dlg.t"); // nothing leads to it
  std::vector<std::string> errors{}, warnings{};
  build(t, makeEnv(), errors, warnings);
  CHECK(errors.size() == 3);
  CHECK(warnings.size() == 1); // the lost line

  Tree empty{};
  errors.clear();
  build(empty, makeEnv(), errors, warnings);
  CHECK(errors.size() == 1); // no Start
}
