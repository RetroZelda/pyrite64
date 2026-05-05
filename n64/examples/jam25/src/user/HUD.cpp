#include "globals.h"
#include "script/userScript.h"
#include "systems/context.h"
#include "ui/UI.h"

namespace P64::Script::C7D6052B4FD1AF1C
{
  P64_DATA(
    float coinTimer = 0.0f;
  );

  // Model callback: game logic fills HudData each frame.
  // HudData persists in UIModelView between frames so displayCoins animates correctly.
  // Sprites are baked into the canvas and drawn by the auto-generated view function.
  static void hudModel(Object& /*obj*/, Data* data,
                       P64::UI::Types::HudData& hud, float deltaTime)
  {
    hud.health      = User::ctx.health;
    hud.healthTotal = User::ctx.healthTotal;

    // Coin counter: snap up immediately, decrease slowly
    if (hud.displayCoins < (int32_t)User::ctx.coins) {
      hud.displayCoins = (int32_t)User::ctx.coins;
      return;
    }
    if (hud.displayCoins > (int32_t)User::ctx.coins) {
      data->coinTimer += deltaTime;
      if (data->coinTimer > 0.1f) {
        data->coinTimer = 0.0f;
        hud.displayCoins -= 1;
      }
    }
  }

  void init(Object& obj, Data* data)
  {
    using namespace P64::UI;
    bindModel<UISceneType::Hud>(ModelCallback<UISceneType::Hud>(obj, data, hudModel));
    setSceneState(UISceneType::Hud, true);
  }

  void destroy(Object& /*obj*/, Data* /*data*/)
  {
    P64::UI::unbindModel<P64::UI::UISceneType::Hud>();
    P64::UI::setSceneState(P64::UI::UISceneType::Hud, false);
  }
}
