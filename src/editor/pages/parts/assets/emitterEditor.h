/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once

namespace Project::Assets { struct Emitter; }

namespace Editor
{
  // Draws the full particle-emitter settings UI (emission, lifetime, shape, motion,
  // rotation, size/color over life, texture + swaps, animation, material/blend).
  // Rendered inline inside the Asset inspector — there is no separate editor window.
  // Edits `em` in place; the caller persists the .emitter file.
  void drawEmitterSettings(Project::Assets::Emitter &em);
}
