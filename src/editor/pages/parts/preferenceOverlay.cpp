/**
* @copyright 2026 - Nolan Baker
* @license MIT
*/

#include "preferenceOverlay.h"

#include "imgui.h"
#include "../../../context.h"
#include "../../../utils/logger.h"
#include "misc/cpp/imgui_stdlib.h"
#include "../../imgui/helper.h"
#include "../../keymap.h"


bool Editor::PreferenceOverlay::draw()
{
  if (ImGui::CollapsingHeader("Navigation", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImTable::start("Navigation");
    ImTable::add("Zoom Speed", ctx.prefs.zoomSpeed);
    ImTable::add("WASD Move Speed", ctx.prefs.moveSpeed);
    ImTable::add("Modify Move Speed with Wheel", ctx.prefs.mouseWheelModifiesSpeed);
    ImTable::add("Pan Speed", ctx.prefs.panSpeed);
    ImTable::add("Look Speed", ctx.prefs.lookSpeed);
    ImTable::add("Invert Wheel Y", ctx.prefs.invertWheelY);
    ImTable::end();
  }

  if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImTable::start("Rendering");
    bool isOn = ctx.prefs.renderFactorAA  > 1.0f;
    ImTable::add("Anti-Alias", isOn);
    ctx.prefs.renderFactorAA = isOn ? 2.0f : 1.0f;

    if(ctx.forceVSync) {
      ctx.prefs.useVSync = true;
      ImGui::BeginDisabled();
    }
    ImTable::add("VSync", ctx.prefs.useVSync);
    if(ctx.forceVSync)    {
      ImGui::EndDisabled();
      ImGui::SetItemTooltip("GPU only supports VSync");
    }

    if(!ctx.prefs.useVSync)
    {
      ImTable::add("FPS Limit", ctx.prefs.fpsLimit);
      ctx.prefs.fpsLimit = std::max(20, ctx.prefs.fpsLimit);
    }

    ImTable::end();
  }

  if (ImGui::CollapsingHeader("Lines", ImGuiTreeNodeFlags_DefaultOpen)) 
  {
    ImTable::start("Lines");
    ImTable::add("Grid Line Thickness", ctx.prefs.gridLineThickness);
    ImTable::add("AABB Line Thickness", ctx.prefs.aabbLineThickness);
    ImTable::add("Collider Line Thickness", ctx.prefs.colliderLineThickness);
    ImTable::add("Mesh Line Thickness", ctx.prefs.meshLineThickness);
    ImTable::add("Camera Line Thickness", ctx.prefs.cameraLineThickness);
    ImTable::add("Culled Line Thickness", ctx.prefs.culledLineThickness);
    ImTable::add("Light Line Thickness", ctx.prefs.lightLineThickness);

    ImTable::add("AABB Line Color", ctx.prefs.aabbLineColor);
    ImTable::add("Collider Line Color", ctx.prefs.colliderLineColor);
    ImTable::add("Mesh Line Color", ctx.prefs.meshLineColor);
    ImTable::add("Mesh Line Color(Selected)", ctx.prefs.meshLineColorSelected);
    ImTable::add("Camera Line Color", ctx.prefs.cameraLineColor);
    ImTable::add("Camera Line Color(Selected)", ctx.prefs.cameraLineColorSelected);
    ImTable::add("Culled Line Color", ctx.prefs.culledLineColor);
    
    ImTable::end();
  }

  if (ImGui::CollapsingHeader("Keymap", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImTable::start("Keymap");
    if (ImTable::addComboBox("Preset", (int&)ctx.prefs.keymapPreset, { "Blender", "Industry Compatible" })) {
      ctx.prefs.applyKeymapPreset();
    }
    ImTable::end();

    Editor::Input::Keymap defaults = ctx.prefs.getCurrentKeymapPreset();
    if (ImGui::TreeNodeEx("Global", ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen)) {
      ImTable::start("Global");
      ImTable::addKeybind("Save",          ctx.prefs.keymap.save,         defaults.save);
      ImTable::addKeybind("Copy",          ctx.prefs.keymap.copy,         defaults.copy);
      ImTable::addKeybind("Paste",         ctx.prefs.keymap.paste,        defaults.paste);
      ImTable::addKeybind("Reload Assets", ctx.prefs.keymap.reloadAssets, defaults.reloadAssets);
      ImTable::addKeybind("Build",         ctx.prefs.keymap.build,        defaults.build);
      ImTable::addKeybind("Build & Run",   ctx.prefs.keymap.buildAndRun,  defaults.buildAndRun);
      ImTable::addKeybind("Zoom In",       ctx.prefs.keymap.zoomIn,       defaults.zoomIn);
      ImTable::addKeybind("Zoom Out",      ctx.prefs.keymap.zoomOut,      defaults.zoomOut);
      ImTable::end();
      ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("3D View", ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen)) {
      ImTable::start("3D View");
      ImTable::addKeybind("Move Forward",    ctx.prefs.keymap.moveForward,    defaults.moveForward);
      ImTable::addKeybind("Move Back",       ctx.prefs.keymap.moveBack,       defaults.moveBack);
      ImTable::addKeybind("Move Left",       ctx.prefs.keymap.moveLeft,       defaults.moveLeft);
      ImTable::addKeybind("Move Right",      ctx.prefs.keymap.moveRight,      defaults.moveRight);
      ImTable::addKeybind("Move Up",         ctx.prefs.keymap.moveUp,         defaults.moveUp);
      ImTable::addKeybind("Move Down",       ctx.prefs.keymap.moveDown,       defaults.moveDown);
      ImTable::addKeybind("Toggle Ortho",    ctx.prefs.keymap.toggleOrtho,    defaults.toggleOrtho);
      ImTable::addKeybind("Focus Object",    ctx.prefs.keymap.focusObject,    defaults.focusObject);
      ImTable::addKeybind("Gizmo Translate", ctx.prefs.keymap.gizmoTranslate, defaults.gizmoTranslate);
      ImTable::addKeybind("Gizmo Rotate",    ctx.prefs.keymap.gizmoRotate,    defaults.gizmoRotate);
      ImTable::addKeybind("Gizmo Scale",     ctx.prefs.keymap.gizmoScale,     defaults.gizmoScale);
      ImTable::addKeybind("Delete Object",   ctx.prefs.keymap.deleteObject,   defaults.deleteObject);
      ImTable::addKeybind("Snap Object",     ctx.prefs.keymap.snapObject,     defaults.snapObject);
      ImTable::end();
      ImGui::TreePop();
    }
  }

  if (ImGui::Button("Save")) {
    ctx.prefs.save();
    return true;
  }
  return false;
}