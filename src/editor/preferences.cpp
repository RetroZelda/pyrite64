/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "preferences.h"

#include "../utils/prop.h"
#include "../utils/json.h"
#include "../utils/jsonBuilder.h"
#include "../utils/proc.h"

namespace fs = std::filesystem;

namespace
{
  fs::path getPrefsPath() {
    return Utils::Proc::getAppDataPath() / "preferences.json";
  }

  constexpr Editor::Preferences DEF{};
}

void Editor::Preferences::load()
{
  auto doc = Utils::JSON::loadFile(getPrefsPath());
  if(doc.is_object()) {
    keymapPreset = (Editor::Input::KeymapPreset)doc.value("keymapPreset", 0);
    if (doc.contains("keymap")) keymap.deserialize(doc["keymap"], keymapPreset);
    else applyKeymapPreset();

    zoomSpeed = doc.value("zoomSpeed", DEF.zoomSpeed);
    moveSpeed = doc.value("moveSpeed", DEF.moveSpeed);
    panSpeed = doc.value("panSpeed", DEF.panSpeed);
    lookSpeed = doc.value("lookSpeed", DEF.lookSpeed);
    invertWheelY = doc.value("invertWheelY", DEF.invertWheelY);
    renderFactorAA = doc.value("renderFactorAA", DEF.renderFactorAA);
    useVSync = doc.value("useVSync", DEF.useVSync);
    fpsLimit = doc.value("fpsLimit", DEF.fpsLimit);
    showRotAsEuler = doc.value("showRotAsEuler", DEF.showRotAsEuler);
    mouseWheelModifiesSpeed = doc.value("mouseWheelModifiesSpeed", DEF.mouseWheelModifiesSpeed);
    gridLineThickness = doc.value("gridLineThickness", DEF.gridLineThickness);
    colliderLineThickness = doc.value("colliderLineThickness", DEF.colliderLineThickness);
    meshLineThickness = doc.value("meshLineThickness", DEF.meshLineThickness);
    cameraLineThickness = doc.value("cameraLineThickness", DEF.cameraLineThickness);
    culledLineThickness = doc.value("culledLineThickness", DEF.culledLineThickness);
    lightLineThickness = doc.value("lightLineThickness", DEF.lightLineThickness);

    colliderLineColor.r = doc.value("colliderLineColor_r", DEF.colliderLineColor.r);
    colliderLineColor.g = doc.value("colliderLineColor_g", DEF.colliderLineColor.g);
    colliderLineColor.b = doc.value("colliderLineColor_b", DEF.colliderLineColor.b);
    colliderLineColor.a = doc.value("colliderLineColor_a", DEF.colliderLineColor.a);
    meshLineColor.r = doc.value("meshLineColor_r", DEF.meshLineColor.r);
    meshLineColor.g = doc.value("meshLineColor_g", DEF.meshLineColor.g);
    meshLineColor.b = doc.value("meshLineColor_b", DEF.meshLineColor.b);
    meshLineColor.a = doc.value("meshLineColor_a", DEF.meshLineColor.a);
    meshLineColorSelected.r = doc.value("meshLineColorSelected_r", DEF.meshLineColorSelected.r);
    meshLineColorSelected.g = doc.value("meshLineColorSelected_g", DEF.meshLineColorSelected.g);
    meshLineColorSelected.b = doc.value("meshLineColorSelected_b", DEF.meshLineColorSelected.b);
    meshLineColorSelected.a = doc.value("meshLineColorSelected_a", DEF.meshLineColorSelected.a);
    cameraLineColor.r = doc.value("cameraLineColor_r", DEF.cameraLineColor.r);
    cameraLineColor.g = doc.value("cameraLineColor_g", DEF.cameraLineColor.g);
    cameraLineColor.b = doc.value("cameraLineColor_b", DEF.cameraLineColor.b);
    cameraLineColor.a = doc.value("cameraLineColor_a", DEF.cameraLineColor.a);
    cameraLineColorSelected.r = doc.value("cameraLineColorSelected_r", DEF.cameraLineColorSelected.r);
    cameraLineColorSelected.g = doc.value("cameraLineColorSelected_g", DEF.cameraLineColorSelected.g);
    cameraLineColorSelected.b = doc.value("cameraLineColorSelected_b", DEF.cameraLineColorSelected.b);
    cameraLineColorSelected.a = doc.value("cameraLineColorSelected_a", DEF.cameraLineColorSelected.a);
    culledLineColor.r = doc.value("culledLineColor_r", DEF.culledLineColor.r);
    culledLineColor.g = doc.value("culledLineColor_g", DEF.culledLineColor.g);
    culledLineColor.b = doc.value("culledLineColor_b", DEF.culledLineColor.b);
    culledLineColor.a = doc.value("culledLineColor_a", DEF.culledLineColor.a);
  } else {
    applyKeymapPreset();
  }
}

void Editor::Preferences::save()
{
  std::string json = Utils::JSON::Builder{}
    .set("keymapPreset", (uint32_t)keymapPreset)
    .set("keymap", keymap.serialize(keymapPreset))
    .set("zoomSpeed", zoomSpeed)
    .set("moveSpeed", moveSpeed)
    .set("panSpeed", panSpeed)
    .set("lookSpeed", lookSpeed)
    .set("invertWheelY", invertWheelY)
    .set("renderFactorAA", renderFactorAA)
    .set("useVSync", useVSync)
    .set("fpsLimit", fpsLimit)
    .set("showRotAsEuler", showRotAsEuler)
    .set("mouseWheelModifiesSpeed", mouseWheelModifiesSpeed)
    .set("gridLineThickness", gridLineThickness)
    .set("colliderLineThickness", colliderLineThickness)
    .set("meshLineThickness", meshLineThickness)
    .set("cameraLineThickness", cameraLineThickness)
    .set("culledLineThickness", culledLineThickness)
    .set("lightLineThickness", lightLineThickness)

    .set("colliderLineColor_r", colliderLineColor.r)
    .set("colliderLineColor_g", colliderLineColor.g)
    .set("colliderLineColor_b", colliderLineColor.b)
    .set("colliderLineColor_a", colliderLineColor.a)
    .set("meshLineColor_r", meshLineColor.r)
    .set("meshLineColor_g", meshLineColor.g)
    .set("meshLineColor_b", meshLineColor.b)
    .set("meshLineColor_a", meshLineColor.a)
    .set("meshLineColorSelected_r", meshLineColorSelected.r)
    .set("meshLineColorSelected_g", meshLineColorSelected.g)
    .set("meshLineColorSelected_b", meshLineColorSelected.b)
    .set("meshLineColorSelected_a", meshLineColorSelected.a)
    .set("cameraLineColor_r", cameraLineColor.r)
    .set("cameraLineColor_g", cameraLineColor.g)
    .set("cameraLineColor_b", cameraLineColor.b)
    .set("cameraLineColor_a", cameraLineColor.a)
    .set("cameraLineColorSelected_r", cameraLineColorSelected.r)
    .set("cameraLineColorSelected_g", cameraLineColorSelected.g)
    .set("cameraLineColorSelected_b", cameraLineColorSelected.b)
    .set("cameraLineColorSelected_a", cameraLineColorSelected.a)
    .set("culledLineColor_r", culledLineColor.r)
    .set("culledLineColor_g", culledLineColor.g)
    .set("culledLineColor_b", culledLineColor.b)
    .set("culledLineColor_a", culledLineColor.a)
    .toString();
  auto prefPath = getPrefsPath();
  printf("Saving prefs to %s\n", prefPath.c_str());
  Utils::FS::saveTextFile(prefPath, json);
}

void Editor::Preferences::applyKeymapPreset()
{
  keymap = getCurrentKeymapPreset();
}

Editor::Input::Keymap Editor::Preferences::getCurrentKeymapPreset() const
{
  if (keymapPreset == Input::KeymapPreset::Blender) {
    return Input::blenderKeymap;
  }
  return Input::standardKeymap;
}
