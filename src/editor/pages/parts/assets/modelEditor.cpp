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

  if(placeholderOverflow) {
    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
      ICON_MDI_ALERT " At most %d texture placeholders per model. Extra ones were disabled.",
      Project::Assets::MaterialTex::MAX_PLACEHOLDERS);
  }

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

  // Assign placeholder slot indices, capped at the runtime slot limit.
  uint32_t slot = 0;
  placeholderOverflow = false;
  auto assignSlot = [&](Project::Assets::MaterialTex &tex) {
    if(!tex.dynType.value)return;
    if(slot >= (uint32_t)Project::Assets::MaterialTex::MAX_PLACEHOLDERS) {
      tex.dynType.value = Project::Assets::MaterialTex::DYN_TYPE_NONE;
      placeholderOverflow = true;
      return;
    }
    tex.dynPlaceholder.value = slot++;
  };
  for(auto &entry : model->model.materials)
  {
    auto &mat = entry.second;
    if(!mat.isCustom.value)continue;
    assignSlot(mat.tex0);
    assignSlot(mat.tex1);
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
