/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "projectSettings.h"

#include "imgui.h"
#include "../../../context.h"
#include "../../../utils/logger.h"
#include "misc/cpp/imgui_stdlib.h"
#include "../../imgui/helper.h"

bool Editor::ProjectSettings::draw()
{
  ImGui::BeginChild("TOP", ImVec2(0, -26_px));

  if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImTable::start("General");
    ImTable::add("Name", ctx.project->conf.name);
    ImTable::add("ROM-Name", ctx.project->conf.romName);
    ImTable::end();
  }
  if (ImGui::CollapsingHeader("Collision", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImTable::start("Collision");

    ImTable::add("Layer Names");
    for(int i=0; i<8; ++i) {
      ImTable::add("Layer " + std::to_string(i));
      ImGui::InputText(("##" + std::to_string(i)).c_str(), &ctx.project->conf.collLayerNames[i]);
    }
    ImTable::end();
  }

  if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImTable::start("Environment");
    ImTable::addPath("Emulator", ctx.project->conf.pathEmu);
    ImTable::addPath("N64_INST", ctx.project->conf.pathN64Inst, true, "$N64_INST");
    ImTable::end();
  }

  ImGui::EndChild();

  ImGui::BeginChild("BOTTOM", ImVec2(0, 24_px));
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 64_px);

    bool res = ImGui::Button("Save", ImVec2(60_px, 0));
  ImGui::EndChild();

  if (res) {
    ctx.project->save();
  }
  return res;
}
