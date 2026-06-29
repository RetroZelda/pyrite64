/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>
//
// Small UI component-script types shared between the generated UISceneTypes.h
// data structs and the UI runtime (UI.h). Kept separate so UISceneTypes.h — which
// UI.h includes first — can reference UIElemState without a circular include.
//
namespace P64::UI
{
    // Per-element component-script state. A fixed flat blob (mirrors the Code
    // component's flat buffer); the script's Data lives here. Must be >= the
    // script's DATA_SIZE (asserted at runtime on init).
    static constexpr unsigned MAX_UI_ELEM_DATA = 256;
    struct UIElemState { alignas(8) unsigned char data[MAX_UI_ELEM_DATA]; };

    // Phase passed to a canvas's generated "behavior" function (element scripts +
    // focus navigation).
    enum class BehaviorPhase : uint8_t { Init, Destroy, Update, Event, FocusInput };

    // ---- Animation timeline runtime state (Phase 5) -----------------------
    // Per-animated-element channel offsets, composited on top of base draw values.
    struct AnimState
    {
        float   dx{0.0f}, dy{0.0f};   // position offset (px)
        float   drot{0.0f};           // rotation offset (added to theta)
        float   scale{1.0f};          // scale multiplier
        float   alpha{1.0f};          // alpha multiplier (0..1)
        uint8_t color[4]{255,255,255,255};
        uint8_t hasColor{0};          // 1 = color channel overrides prim color
        int32_t frameIndex{0};        // sprite-sheet frame
    };

    // Per-clip playback state.
    struct AnimPlayback { float elapsed{0.0f}, prevElapsed{0.0f}; uint8_t playing{0}, finished{0}; };

    // Max concurrent animation clips per canvas (fixed runtime array).
    static constexpr uint8_t MAX_UI_ANIMS = 16;
}
