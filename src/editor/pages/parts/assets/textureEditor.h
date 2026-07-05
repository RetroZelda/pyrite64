/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include "../../../../project/assets/material.h"

namespace Editor::TextureEditor
{
  // Draws the texture picker + preview. `tileParams` adds the Offset/Scale/Repeat/Mirror tile
  // settings — mesh materials bake those, emitters do NOT (tpx has a fixed 8x8 UV mapping),
  // so emitters pass false to avoid dead UI.
  void draw(Project::Assets::MaterialTex &tex, bool tileParams = true);
}
