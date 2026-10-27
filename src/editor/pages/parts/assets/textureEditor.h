/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include "../../../../project/assets/material.h"

namespace Editor::TextureEditor
{
  /**
   * Draws the texture settings of a material slot.
   * @param modelUUID asset the material belongs to (0 = none), e.g. a model or an emitter.
   *        The texture picker opens in that asset's folder.
   * @param tileParams adds the Offset/Scale/Repeat/Mirror tile settings: mesh materials bake
   *        those, emitters do NOT (tpx has a fixed 8x8 UV mapping), so emitters pass false.
   */
  void draw(Project::Assets::MaterialTex &tex, uint64_t modelUUID = 0, bool tileParams = true);

  /**
   * Texture field with a folder-based picker popup (thumbnails, breadcrumbs, search).
   * Opens in startDir (project-relative, e.g. "assets/characters/hero"), or "assets" if empty.
   * Accepts image assets dragged in from the asset browser.
   * @return true if the selection changed
   */
  bool drawTexturePicker(const char *label, uint64_t &texUUID, const std::string &startDir);
}
