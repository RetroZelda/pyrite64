#include "script/globalScript.h"
#include "debug/debugDraw.h"
#include "ui/UI.h"
#include "vi/swapChain.h"

namespace P64::UI::Generated { void uiInitializeBindings(); }

// A global script with pins: the system "Score". Anything wired to global::Score.Add adds a point;
// global::Score.Changed fires with the new total. It also drives the UI canvases, like a game's
// global setup script does, and prints what happens.
namespace P64::GlobalScript::E7A3C1B2D4F50601
{
  P64_GLOBAL_EVENTS_IN(Score, Add, Clear);
  P64_GLOBAL_EVENTS_OUT(Score, Changed);

  constinit uint32_t score = 0;

  void onInput(ScoreIn input, const Events::Event& ev)
  {
    score = (input == ScoreIn::Add) ? score + 1 : 0;
    Events::post("global::Score.Changed"_event, score);
  }

  void onScenePreLoad() { P64::UI::init(); }
  void onScenePostLoad() { P64::UI::Generated::uiInitializeBindings(); }
  void onScenePostUnload() { P64::UI::destroy(); }
  void onSceneUpdate() { P64::UI::update(VI::SwapChain::getDeltaTime()); }

  void onSceneDraw2D()
  {
    P64::UI::draw_2D();
    Debug::printStart();
    Debug::printf(24, 200, "Doors opened: %lu", (unsigned long)score);
    Debug::print(24, 212, "Opens every 3 ticks. A: close");
  }
}
