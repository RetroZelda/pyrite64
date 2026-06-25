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
    ImGui::Combo("##fm", &em.fireMode.value, "Continuous\0One-Shot\0");

    if(em.fireMode.value == Em::FIRE_ONE_SHOT) {
      ImTable::addProp("Burst Count", em.burstCount);
    } else {
      ImTable::addProp("Spawn Rate", em.spawnRate);
    }
    ImTable::addProp("Max Particles", em.maxParticles);
    em.maxParticles.value = glm::clamp(em.maxParticles.value, 2u, 2048u);

    ImTable::add("Sim Space");
    int space = em.worldSpace.value ? 1 : 0;
    if(ImGui::Combo("##space", &space, "Relative to Emitter\0World Space\0")) {
      em.worldSpace.value = (space == 1);
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

  // Primary texture (the material's tex0). Tile/scale/mirror settings here apply to
  // whichever texture is bound at runtime, including the swap-list entries below.
  if(ImGui::CollapsingSubHeader("Texture", ImGuiTreeNodeFlags_DefaultOpen) && ImTable::start("Texture", nullptr, labelWidth))
  {
    TextureEditor::draw(em.material.tex0);
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
    ImTable::addProp("Frames", em.texFrames);
    em.texFrames.value = glm::clamp(em.texFrames.value, 1u, 64u);

    ImTable::add("Frame Mode");
    ImGui::Combo("##frameMode", &em.frameMode.value, "Off\0Loop\0Over Lifetime\0Random\0");
    ImTable::addProp("Frame Rate", em.frameRate);

    ImTable::addProp("UV Scroll", em.uvScrollSpeed);

    ImTable::add("Swap Mode");
    ImGui::Combo("##swapMode", &em.swapMode.value, "Off\0Loop\0Over Lifetime\0");
    ImTable::addProp("Swap Rate", em.swapRate);

    ImTable::addProp("Mirror Frames", em.mirrorAnim);
  });

  if(ImGui::CollapsingHeader("Material / Blend", ImGuiTreeNodeFlags_DefaultOpen))
  {
    // Emitters bind one texture at a time and manage it via the Texture section above,
    // so skip the placeholder-based texture sub-sections here.
    Editor::Helpers::drawMaterialSettings(em.material, labelWidth, /*includeTextures*/ false, /*usePlaceholder*/ false);
  }
}
