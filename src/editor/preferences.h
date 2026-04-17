/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include "keymap.h"
#include <glm/glm.hpp>

namespace Editor
{
  struct Preferences
  {
    Input::KeymapPreset keymapPreset{Input::KeymapPreset::Blender};
    Input::Keymap keymap{};
    float zoomSpeed = 1.0f;
    float moveSpeed = 120.0f;
    float panSpeed = 30.0f;
    float lookSpeed = -10.0f;
    bool invertWheelY = false;
    float renderFactorAA = 1.0f;
    bool useVSync = false;
    int fpsLimit = 60;
    bool showRotAsEuler = false;
    bool mouseWheelModifiesSpeed = false;

    float gridLineThickness = 0.05f;
    float aabbLineThickness = 2.5f;
    float colliderLineThickness = 2.5f;
    float meshLineThickness = 2.5f;
    float cameraLineThickness = 2.5f;
    float culledLineThickness = 2.5f;
    float lightLineThickness = 2.5f;
    glm::vec4 aabbLineColor         = {0.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 colliderLineColor     = {0.0f, 0.88f, 0.88f, 1.0f};
    glm::vec4 meshLineColor         = {0.66f, 0.66f, 0.66f, 1.0f};
    glm::vec4 meshLineColorSelected = {1.0f, 0.66f, 0.0f, 1.0f};
    glm::vec4 cameraLineColor       = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 cameraLineColorSelected = {1.0f, 0.7, 0.18f, 1.0f};
    glm::vec4 culledLineColor       = {1.0f, 0.0f, 0.0f, 1.0f};

    // ctx.prefs.culledLineThickness
    void load();
    void save();

    void applyKeymapPreset();
    Input::Keymap getCurrentKeymapPreset() const;
  };
}
