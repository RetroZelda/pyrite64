/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: interactions (docs/todo/interactions/PLAN.md, section 10). Condition inputs on scripts that
// are not conditions, and the viewport arrow a script asks for.
#include "doctest/doctest.h"
#include <string>
#include "utils/codeParser.h"

namespace
{
  Utils::CPP::Condition parse(const std::string &code)
  {
    return Utils::CPP::parseCondition(code, Utils::CPP::parseEventPins(code));
  }

  template<typename T>
  bool hasError(const T &res, const std::string &part)
  {
    for(const auto &err : res.errors) {
      if(err.find(part) != std::string::npos) return true;
    }
    return false;
  }

  constexpr const char* INTERACTION = R"(
namespace P64::Script::C0FFEE00000000A1
{
  P64_DATA( [[P64::Name("Verb")]] uint8_t verb = 0; );
  P64_EVENTS_IN(Enable, Disable, CanUse);
  P64_EVENTS_OUT(Used);
  P64_CONDITION_INPUTS(CanUse);
  void onInput(Object& obj, Data *data, In input, const Events::Event& ev) {}
}
)";
}

TEST_SUITE("interactions")
{
  TEST_CASE("condition inputs: an ordinary script may read conditions on an input pin")
  {
    auto res = parse(INTERACTION);
    CHECK_FALSE(res.isCondition);
    CHECK(res.errors.empty());
    REQUIRE(res.inputs.size() == 1);
    CHECK(res.readsConditions("CanUse"));
    CHECK_FALSE(res.readsConditions("Enable"));
    CHECK(res.triggers.empty());
  }

  TEST_CASE("condition inputs: the pin must be an input of the script, in any script")
  {
    auto res = parse("P64_DATA(float a;);\nP64_EVENTS_IN(A);\nP64_EVENTS_OUT(B);\nP64_CONDITION_INPUTS(B);");
    CHECK(res.inputs.empty());
    CHECK(hasError(res, "'B' is not an input pin"));

    auto twice = parse("P64_DATA(float a;);\nP64_EVENTS_IN(A, C);\nP64_CONDITION_INPUTS(A);\nP64_CONDITION_INPUTS(C);");
    CHECK(hasError(twice, "is used 2 times"));
  }

  TEST_CASE("condition inputs: triggers still need a condition script")
  {
    auto res = parse("P64_DATA(float a;);\nP64_EVENTS_IN(A);\nP64_CONDITION_INPUTS(A);\nP64_CONDITION_TRIGGERS(\"scene::Loaded\"_event);");
    CHECK(hasError(res, "P64_CONDITION_TRIGGERS only works in a condition script"));
    CHECK_FALSE(hasError(res, "P64_CONDITION_INPUTS only works"));
    CHECK(res.readsConditions("A"));
  }

  TEST_CASE("condition inputs: a condition script reads them as before")
  {
    auto res = parse(R"(
      P64_CONDITION_DATA();
      P64_EVENTS_IN(Inputs);
      P64_CONDITION_INPUTS(Inputs);
      void onInput(Object& obj, Data *data, In input, const Events::Event& ev) {}
      bool evaluate(Object& obj, Data *data) { return true; }
    )");
    CHECK(res.isCondition);
    CHECK(res.errors.empty());
    CHECK(res.readsConditions("Inputs"));
  }

  TEST_CASE("editor arrow: absent, plain, coloured, coloured with a length")
  {
    auto none = Utils::CPP::parseEditorHints(INTERACTION);
    CHECK_FALSE(none.arrow);
    CHECK(none.errors.empty());

    auto plain = Utils::CPP::parseEditorHints("P64_DATA();\nP64_EDITOR_ARROW();");
    CHECK(plain.arrow);
    CHECK(plain.errors.empty());
    CHECK(plain.arrowLength == doctest::Approx(8.0f));

    auto colour = Utils::CPP::parseEditorHints("P64_EDITOR_ARROW(0.2, 0.9f, 1);");
    CHECK(colour.arrow);
    CHECK(colour.errors.empty());
    CHECK(colour.arrowColor[0] == doctest::Approx(0.2f));
    CHECK(colour.arrowColor[1] == doctest::Approx(0.9f));
    CHECK(colour.arrowColor[2] == doctest::Approx(1.0f));
    CHECK(colour.arrowLength == doctest::Approx(8.0f));

    auto length = Utils::CPP::parseEditorHints("P64_EDITOR_ARROW(0, 0, 0, 12.5);");
    CHECK(length.errors.empty());
    CHECK(length.arrowLength == doctest::Approx(12.5f));
  }

  TEST_CASE("editor arrow: wrong values are reported and the defaults kept")
  {
    auto count = Utils::CPP::parseEditorHints("P64_EDITOR_ARROW(1, 0);");
    CHECK(count.arrow);
    CHECK(hasError(count, "takes no values, three"));

    auto range = Utils::CPP::parseEditorHints("P64_EDITOR_ARROW(1.5, 0, 0);");
    CHECK(hasError(range, "'1.5' is not a number from 0 to 1"));
    CHECK(range.arrowColor[0] == doctest::Approx(1.0f));
    CHECK(range.arrowColor[1] == doctest::Approx(0.6f));

    auto text = Utils::CPP::parseEditorHints("P64_EDITOR_ARROW(red, 0, 0);");
    CHECK(hasError(text, "'red' is not a number"));

    auto badLength = Utils::CPP::parseEditorHints("P64_EDITOR_ARROW(1, 1, 1, 0);");
    CHECK(hasError(badLength, "'0' is not a length above 0"));
    CHECK(badLength.arrowLength == doctest::Approx(8.0f));
  }

  TEST_CASE("editor arrow: a mention in a comment or the macro's definition is not a request")
  {
    auto comment = Utils::CPP::parseEditorHints("// P64_EDITOR_ARROW();\n/* P64_EDITOR_ARROW(1,1,1); */\nP64_DATA();");
    CHECK_FALSE(comment.arrow);

    auto define = Utils::CPP::parseEditorHints("#define P64_EDITOR_ARROW(...) static_assert(true, \"\")\n");
    CHECK_FALSE(define.arrow);

    auto longer = Utils::CPP::parseEditorHints("MY_P64_EDITOR_ARROW();");
    CHECK_FALSE(longer.arrow);
  }
}
