/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "helper.h"

#include "imgui_internal.h"
#include "../../context.h"

namespace
{
  constexpr ImVec4 COLOR_NONE{0,0,0,0};
  constinit ImGuiKeyChord* rebindingChord{nullptr};
}

namespace ImTable {
  constinit Project::Object* obj{nullptr};
}

bool ImGui::IconButton(const char* label, const ImVec2 &labelSize, const ImVec4 &color)
{
  ImVec2 min = GetCursorScreenPos();
  ImVec2 max = ImVec2(min.x + labelSize.x, min.y + labelSize.y);
  bool hovered = IsMouseHoveringRect(min, max);
  bool clicked = IsMouseClicked(ImGuiMouseButton_Left) && hovered;

  if(hovered)SetMouseCursor(ImGuiMouseCursor_Hand);
  PushStyleColor(ImGuiCol_Text,
    hovered ? GetStyleColorVec4(ImGuiCol_DragDropTarget) : color
  );

  PushStyleColor(ImGuiCol_Button, COLOR_NONE);
  PushStyleColor(ImGuiCol_ButtonActive, COLOR_NONE);
  PushStyleColor(ImGuiCol_ButtonHovered, COLOR_NONE);

  //SmallButton(label);
  Text("%s", label);

  PopStyleColor(4);
  return clicked;
}

glm::vec3 tmpEuler;
bool ImGui::rotationInput(glm::quat &quat) {
  if(!ctx.prefs.showRotAsEuler) return InputFloat4("##", glm::value_ptr(quat));
  
  glm::quat calcRot = glm::normalize(glm::quat(glm::radians(tmpEuler)));
  glm::quat orgRot = glm::normalize(quat);
  if (glm::dot(calcRot, orgRot) < 1) tmpEuler = glm::degrees(glm::eulerAngles(orgRot));

  if (!InputFloat3("##RotEuler", glm::value_ptr(tmpEuler))) return false;
  quat = glm::normalize(glm::quat(glm::radians(tmpEuler)));
  return true;
}

void ImTable::addMultiSelectMask8(
  const std::string &name,
  uint32_t &valueMask,
  const std::array<std::string, 8> &values,
  const std::string &valueEmpty
) {
    add(name);
    bool disabled = isPrefabLocked();
    if (disabled) ImGui::BeginDisabled();
    std::string labelHidden = "##" + name;

    // Build preview string of selected options
    std::string preview;
    for (int i = 0; i < 8; ++i) {
        if (valueMask & (1 << i)) {
            if (!preview.empty()) preview += "  |  ";
            preview += values[i].empty() ? "??" : values[i];
        }
    }
    bool isEmpty = preview.empty();
    if (isEmpty) {
      preview = valueEmpty;
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }

    auto open = ImGui::BeginCombo(labelHidden.c_str(), preview.c_str(), ImGuiComboFlags_None);
    if(isEmpty) ImGui::PopStyleColor();

    if (open) {
        for (int i = 0; i < 8; ++i) {
            if (values[i].empty()) continue;
            bool selected = (valueMask & (1 << i)) != 0;
            auto valText = std::string{selected
              ? ICON_MDI_CHECKBOX_MARKED " "
              : ICON_MDI_CHECKBOX_BLANK_OUTLINE " "
            } + values[i] + "##" + std::to_string(i);

            if (ImGui::Selectable(valText.c_str(), selected, ImGuiSelectableFlags_DontClosePopups)) {
                selected = !selected;
                if (selected) {
                    valueMask |= (1 << i);
                } else {
                    valueMask &= ~(1 << i);
                }
                Editor::UndoRedo::getHistory().markChanged("Edit " + name);
            }
        }
        ImGui::EndCombo();
    }
    if (disabled) ImGui::EndDisabled();
}

bool ImTable::addKeybind(const std::string &name, ImGuiKeyChord &chord, ImGuiKeyChord defaultValue, bool isChord) {
  add(name);
  ImGui::PushID(name.c_str());

  bool isOverridden = chord != defaultValue;
  float w = isOverridden ? (ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeightWithSpacing()) : -FLT_MIN;

  bool isRebinding = rebindingChord == &chord;

  std::string keyName{};
  const char* label = "Press any key...";
  if(!isRebinding) {
    keyName = Editor::Input::GetKeyChordName(chord);
    label = keyName.empty() ? "<?>" : keyName.c_str();
  }

  if (ImGui::Button(label, ImVec2(w, 0))) {
    rebindingChord = &chord;
  }

  if (isOverridden) {
    ImGui::SameLine(0, 2);
    if (ImGui::Button(ICON_MDI_CLOSE, ImVec2(-FLT_MIN, 0))) {
      chord = defaultValue;
      Editor::UndoRedo::getHistory().markChanged("Reset " + name);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to default key.");
  }

  if (!isRebinding) {
    ImGui::PopID();
    return false;
  }

  ImGuiIO &io = ImGui::GetIO();
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_ReservedForModCtrl; k++) {
    if (!ImGui::IsKeyPressed((ImGuiKey)k)) continue;
    if (isChord && k >= (int)ImGuiKey_LeftCtrl && k <= (int)ImGuiKey_RightSuper) continue;

    ImGuiKeyChord mods = ImGuiKey_None;
    if (isChord) {
      if (io.KeyCtrl)  mods |= ImGuiMod_Ctrl;
      if (io.KeyShift) mods |= ImGuiMod_Shift;
      if (io.KeyAlt)   mods |= ImGuiMod_Alt;
      if (io.KeySuper) mods |= ImGuiMod_Super;
    }

    rebindingChord = nullptr;
    if (k == ImGuiKey_Escape) {
      break;
    } else {
      chord = (ImGuiKey)k | mods;
      Editor::UndoRedo::getHistory().markChanged("Rebind " + name);
      ImGui::PopID();
      return true;
    }
  }

  ImGui::PopID();
  return false;
}

void ImGui::makeTabVisible(const char* tabName)
{
  ImGuiWindow* window = ImGui::FindWindowByName(tabName);
  if (window == NULL || window->DockNode == NULL || window->DockNode->TabBar == NULL) {
    return;
  }
  window->DockNode->TabBar->NextSelectedTabId = window->TabId;
}
