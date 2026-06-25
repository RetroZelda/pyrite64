/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "modelEditor.h"

#include "editorHelpers.h"
#include "../../../../context.h"
#include "../../../imgui/helper.h"

namespace
{
  ImVec2 DEF_WIN_SIZE{400, 400};
}

bool Editor::ModelEditor::draw(ImGuiID defDockId)
{
  auto &assetManager = ctx.project->getAssets();
  auto model = assetManager.getEntryByUUID(assetUUID);
  if(!model)return false;

  winName = "Model: " + model->name;
  ImGui::SetNextWindowSize(DEF_WIN_SIZE, ImGuiCond_FirstUseEver);
  auto screenSize = ImGui::GetMainViewport()->WorkSize;
  ImGui::SetNextWindowPos({(screenSize.x - DEF_WIN_SIZE.x) / 2, (screenSize.y - DEF_WIN_SIZE.y) / 2}, ImGuiCond_FirstUseEver);

  bool isOpen = true;
  ImGui::Begin(winName.c_str(), &isOpen);
  ImGui::Text("Model: %s", model->name.c_str());

  ImVec2 labelWidth = {89_px, -1.0f};
  bool needsReload = false;

  std::string matToRemove{};
  for(auto &entry : model->model.materials)
  {
    auto label = "Material: " + entry.first;
    ImGui::PushID(label.c_str());
    ImGui::SetNextItemAllowOverlap();
    const bool matOpen = ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    {
      const float helpSize = 19_px;
      ImGui::SameLine(ImGui::GetContentRegionMax().x - helpSize - 4_px);
      ImGui::HelpIcon("/manual/editor/materials", "Open Docs", helpSize);
    }
    if (matOpen)
    {
      auto &mat = entry.second;

      ImTable::start("General", nullptr, labelWidth);
      if(ImTable::addProp("Override", mat.isCustom))
      {
        if(mat.isCustom.value) {
          model->conf.data["materials"][entry.first] = mat.serialize();
        } else {
          matToRemove = entry.first; // defer to not break loop
        }
        assetManager.markAssetMetaDirty(model->getUUID());
      }
      ImTable::end();

      if(!mat.isCustom.value)
      {
        ImGui::PopID();
        continue;
      }

      auto oldMat = mat;

      Editor::Helpers::drawMaterialSettings(mat, labelWidth, /*includeTextures*/ true, /*usePlaceholder*/ true);

      ImGui::Dummy({0, 2_px});

      if(mat.isCustom.value && oldMat != mat) {
        model->conf.data["materials"][entry.first] = mat.serialize();
        assetManager.markAssetMetaDirty(model->getUUID());

        if(oldMat.tex0.texSize != mat.tex0.texSize) {
          needsReload = true;
        }
      }
    }
    ImGui::PopID();
  }
  ImGui::End();

  // update placeholder indices
  uint32_t slot = 0;
  for(auto &entry : model->model.materials)
  {
    auto &mat = entry.second;
    if(mat.isCustom.value)
    {
      if(mat.tex0.dynType.value) {
        mat.tex0.dynPlaceholder.value = slot++;
        if(slot >= 8)break;
      }
      if(mat.tex1.dynType.value) {
        mat.tex1.dynPlaceholder.value = slot++;
        if(slot >= 8)break;
      }
    }
  }

  if(!matToRemove.empty())
  {
    model->conf.data["materials"].erase(matToRemove);
    assetManager.markAssetMetaDirty(model->getUUID());
    assetManager.save();
    assetManager.reloadAssetByUUID(model->getUUID());
  }

  if(needsReload)
  {
    assetManager.save();
    assetManager.reloadAssetByUUID(model->getUUID());
  }

  return isOpen;
}

void Editor::ModelEditor::focus() const
{
  ImGui::SetWindowFocus(winName.c_str());
}
