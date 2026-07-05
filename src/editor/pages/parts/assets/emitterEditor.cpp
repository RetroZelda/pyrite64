/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "emitterEditor.h"

#include "editorHelpers.h"
#include "textureEditor.h"
#include "../../../imgui/helper.h"
#include "../../../../project/assets/emitter.h"
#include "../../../../context.h"
#include "engine/include/renderer/particles/particleSim.h"

namespace
{
  constexpr ImVec4 WARN_COL{1.0f, 0.75f, 0.25f, 1.0f};

  constexpr bool isPow2(uint32_t x) { return x > 0 && (x & (x - 1)) == 0; }

  // full-width warning row inside an ImTable
  void warnRow(const char* text)
  {
    ImTable::add("");
    ImGui::PushStyleColor(ImGuiCol_Text, WARN_COL);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
  }
}

void Editor::drawEmitterSettings(Project::Assets::Emitter &em)
{
  using Em = Project::Assets::Emitter;
  ImVec2 labelWidth = {110_px, -1.0f};

  auto subSection = [&labelWidth](const char* name, auto cb)
  {
    if(ImGui::CollapsingSubHeader(name, ImGuiTreeNodeFlags_DefaultOpen) && ImTable::start(name, nullptr, labelWidth))
    {
      cb();
      ImTable::end();
    }
  };

  subSection("Emission", [&]
  {
    ImTable::add("Fire Mode");
    ImGui::Combo("##fm", &em.fireMode.value, "Continuous\0One-Shot\0Fill\0");
    ImGui::SetItemTooltip("Continuous: emit at Spawn Rate forever.\n"
      "One-Shot: emit Burst Count particles at once, then stop.\n"
      "Fill: emit at Spawn Rate until Max Particles have been emitted, then stop.");

    if(em.fireMode.value == Em::FIRE_ONE_SHOT) {
      ImTable::addProp("Burst Count", em.burstCount);
    } else {
      ImTable::addProp("Spawn Rate", em.spawnRate);
    }
    ImTable::addProp("Max Particles", em.maxParticles);
    em.maxParticles.value = glm::clamp(em.maxParticles.value, 2u, 2048u);

    ImTable::add("Sim Space");
    ImGui::Combo("##space", &em.simSpace.value, "Relative to Emitter\0World Space\0Relative to Parent\0");
    ImGui::SetItemTooltip("What spawned particles are anchored to (what they move with):\n"
      "Relative to Emitter: the whole system moves rigidly with the emitter.\n"
      "World Space: baked into the world at spawn.\n"
      "Relative to Parent: spawn from the (moving) emitter, then follow the emitter's\n"
      "PARENT object — e.g. an emitter orbiting a character spreads particles around it\n"
      "and the cloud tracks the character. No parent = behaves like World Space.");
    if(em.simSpace.value == Em::SPACE_PARENT) {
      warnRow("The isolated preview below has no parent object: the anchor stays at the "
              "origin, so this looks like World Space here. Place it in a scene to see "
              "particles follow the parent.");
    }
  });

  subSection("Lifetime", [&]
  {
    ImTable::addProp("Lifetime (s)", em.lifetime);
    ImTable::addProp("Variance", em.lifetimeVariance);
    em.lifetimeVariance.value = glm::clamp(em.lifetimeVariance.value, 0.0f, 1.0f);
  });

  subSection("Shape", [&]
  {
    ImTable::add("Shape");
    ImGui::Combo("##shape", &em.emitShape.value, "Point\0Sphere\0Box\0Cone\0");
    ImTable::addProp("Size", em.shapeSize);
    if(em.emitShape.value != Em::SHAPE_POINT) { // a Point offset == moving the emitter itself
      ImTable::addProp("Offset", em.shapeOffset);
      ImGui::SetItemTooltip("Shape center relative to the emitter position.");
    }
  });

  subSection("Motion", [&]
  {
    ImTable::addProp("Direction", em.velocity);
    ImTable::addProp("Speed Min", em.speedMin);
    ImTable::addProp("Speed Max", em.speedMax);
    ImTable::addProp("Spread (deg)", em.spread);
    ImTable::addProp("Gravity", em.gravity);
    ImTable::addProp("Drag", em.drag);
  });

  subSection("Rotation", [&]
  {
    ImTable::addProp("Init Min (deg)", em.rotationMin);
    ImTable::addProp("Init Max (deg)", em.rotationMax);
    ImTable::addProp("Spin Min (deg/s)", em.spinMin);
    ImTable::addProp("Spin Max (deg/s)", em.spinMax);
    warnRow("Not supported by the N64 particle ucode (billboards can't rotate) — "
            "no effect on hardware or in previews.");
  });

  subSection("Size over Life", [&]
  {
    ImTable::addProp("Start", em.sizeStart);
    ImTable::addProp("End", em.sizeEnd);
  });

  subSection("Color over Life", [&]
  {
    ImTable::addProp("Start", em.colorStart);
    ImTable::addProp("End", em.colorEnd);
  });

  // Primary texture (the material's tex0). Tile params are hidden: emitters don't bake
  // them (tpx maps a fixed 8x8 UV base per particle) — frames/scroll/mirror live in the
  // Texture Animation section instead.
  if(ImGui::CollapsingSubHeader("Texture", ImGuiTreeNodeFlags_DefaultOpen) && ImTable::start("Texture", nullptr, labelWidth))
  {
    TextureEditor::draw(em.material.tex0, /*tileParams*/ false);

    // TMEM check: the RDP can only hold ~4KB of texture (whole sheet is uploaded at once).
    // The source-image size is a worst-case estimate (RGBA16 = 2B/px; CI formats are smaller).
    if(em.material.tex0.texSize.value.x > 0) {
      uint32_t estBytes = (uint32_t)(em.material.tex0.texSize.value.x * em.material.tex0.texSize.value.y) * 2;
      if(estBytes > 4096) {
        warnRow("Texture likely exceeds TMEM (4KB): won't upload on hardware "
                "unless a small color-indexed format is used.");
      }
    }
    ImTable::end();
  }

  subSection("Texture Swaps", [&]
  {
    const auto &images = ctx.project->getAssets().getTypeEntries(Project::FileType::IMAGE);
    int removeIdx = -1;
    for(int i = 0; i < (int)em.swapTextures.size(); ++i)
    {
      ImGui::PushID(i);
      ImTable::add(("Tex " + std::to_string(i + 1)).c_str());
      ImGui::SideBySide(
        [&]{ ImTable::addAssetVecComboBox("##swap", images, em.swapTextures[i]); },
        [&]{ if(ImGui::Button(ICON_MDI_TRASH_CAN_OUTLINE)) removeIdx = i; }
      );
      ImGui::PopID();
    }
    if(removeIdx >= 0) em.swapTextures.erase(em.swapTextures.begin() + removeIdx);

    ImTable::add("");
    if(ImGui::Button(ICON_MDI_PLUS " Add Swap Texture")) {
      em.swapTextures.push_back(0);
    }
  });

  subSection("Texture Animation", [&]
  {
    // hardware budget: per-particle tex offsets address 8 sections of 8px (u8, 1/4px units);
    // mirrored playback doubles the logical frames, halving the real ones that fit.
    const uint32_t maxFrames = em.mirrorAnim.value
      ? P64::ParticleSim::MAX_FRAME_SECTIONS / 2 : P64::ParticleSim::MAX_FRAME_SECTIONS;

    ImTable::addProp("Frames", em.texFrames);
    em.texFrames.value = glm::clamp(em.texFrames.value, 1u, maxFrames);

    ImTable::add("Frame Mode");
    ImGui::Combo("##frameMode", &em.frameMode.value, "Off\0Loop\0Over Lifetime\0Random\0");
    ImTable::addProp("Frame Rate", em.frameRate);

    ImTable::addProp("UV Scroll", em.uvScrollSpeed);

    ImTable::add("Swap Mode");
    ImGui::Combo("##swapMode", &em.swapMode.value, "Off\0Loop\0Over Lifetime\0");
    ImTable::addProp("Swap Rate", em.swapRate);

    ImTable::addProp("Mirror Frames", em.mirrorAnim);

    // frame layout check: tpx fits ONE frame onto its 8x8 UV base via a power-of-two shift,
    // so sheets must split into pow2-sized frames of at least 8px.
    if(em.texFrames.value > 1 && em.material.tex0.texSize.value.x > 0) {
      uint32_t sheetW = (uint32_t)em.material.tex0.texSize.value.x;
      uint32_t frames = em.texFrames.value;
      uint32_t frameW = frames <= sheetW ? sheetW / frames : 0;
      if(frameW < 8 || !isPow2(frameW) || (sheetW % frames) != 0) {
        warnRow("Sheet width / Frames must give a power-of-two frame width of at least "
                "8px — frames will sample wrong on hardware.");
      }
    }
  });

  if(ImGui::CollapsingHeader("Material / Blend", ImGuiTreeNodeFlags_DefaultOpen))
  {
    // Emitters bind one texture at a time and manage it via the Texture section above, so
    // skip the texture sub-sections; emitterMode hides everything .emit64 doesn't bake.
    Editor::Helpers::drawMaterialSettings(em.material, labelWidth,
      /*includeTextures*/ false, /*usePlaceholder*/ false, /*emitterMode*/ true);
  }
}
