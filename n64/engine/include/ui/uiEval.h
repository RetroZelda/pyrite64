/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
//
// Shared, dependency-free UI evaluation math.
//
// This header is included by BOTH the auto-generated N64 runtime draw code
// (project src/p64/uiBindings.cpp) AND the host-side editor preview
// (src/editor/pages/parts/canvasViewport.cpp + canvasTimeline.cpp), so the two
// can never drift. It MUST stay free of libdragon / SDL / engine deps — pure
// float/int math only. Each UI feature phase adds its formula here.
//
#include <math.h>

namespace P64::UI::Eval
{
  // --- Easing -------------------------------------------------------------
  // These mirror n64/engine/include/lib/math.h. Linear and the cubic curves are
  // bit-identical; easeOutSin uses the platform sinf (host libm vs newlib),
  // whose divergence is sub-ULP and irrelevant to UI layout parity.
  inline float easeLinear(float t)       { return t; }
  inline float easeOutCubic(float x)     { x = 1.0f - x; return 1.0f - (x * x * x); }
  inline float easeInOutCubic(float x)
  {
    x *= 2.0f;
    if (x < 1.0f) return 0.5f * x * x * x;
    x -= 2.0f;
    return 0.5f * (x * x * x + 2.0f);
  }
  inline float easeOutSin(float x)       { return sinf(x * 1.57079632679489662f); }

  // easeIdx matches Project::CanvasEaseType: 0=Linear, 1=OutCubic, 2=InOutCubic, 3=OutSin
  inline float applyEase(int easeIdx, float t)
  {
    switch (easeIdx)
    {
      case 1:  return easeOutCubic(t);
      case 2:  return easeInOutCubic(t);
      case 3:  return easeOutSin(t);
      default: return t; // Linear
    }
  }

  inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

  // --- Repeater layout (Phase 4) -----------------------------------------
  struct Vec2i { int x; int y; };

  // Per-item placement for a repeated element: row-major grid by `columns`.
  inline Vec2i itemPos(int i, int baseX, int baseY, int columns, int spacingX, int spacingY)
  {
    if (columns < 1) columns = 1;
    const int col = i % columns;
    const int row = i / columns;
    return { baseX + col * spacingX, baseY + row * spacingY };
  }

  // --- Sprite-sheet frame -> sub-rect (Phase 5, Minimal texture scope) ----
  // Maps a frame index into a (s0,t0) top-left within a uniform-grid sheet.
  inline Vec2i frameRect(int frameIndex, int frameW, int frameH, int cols)
  {
    if (cols < 1) cols = 1;
    if (frameIndex < 0) frameIndex = 0;
    const int col = frameIndex % cols;
    const int row = frameIndex / cols;
    return { col * frameW, row * frameH };
  }
}
