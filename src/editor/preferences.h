/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include <string>
#include <vector>
#include "keymap.h"
#include <glm/glm.hpp>

namespace Editor
{
  struct RecentProject
  {
    std::string path;      // absolute path to the .p64proj file
    std::string name;      // display name (project name at the time it was opened)
    std::string cardImage; // optional absolute path to the metadata cart/box art image
  };

  struct Preferences
  {
    Input::KeymapPreset keymapPreset{Input::KeymapPreset::Blender};
    Input::Keymap keymap{};
    std::string themeName{"dark"};
    std::vector<RecentProject> recentProjects{};
    // meters, the viewport works in real-world units
    float zoomSpeed = 0.01f;
    float moveSpeed = 1.2f;
    float panSpeed = 30.0f; // unitless, scaled by the view distance
    float lookSpeed = -10.0f;
    bool invertWheelY = false;
    float renderFactorAA = 1.0f;
    bool useVSync = false;
    int fpsLimit = 60;
    bool showRotAsEuler = false;
    bool mouseWheelModifiesSpeed = false;
    bool viewportLockMode = false;
    float viewportNear = 0.1f;   // editor camera clip planes in meters
    float viewportFar = 1000.0f;

    float gridLineThickness = 0.05f;
    float aabbLineThickness = 0.025f; // meters (world-space line)
    float colliderLineThickness = 0.025f; // meters (world-space line)
    float meshLineThickness = 0.025f; // meters (world-space line)
    float cameraLineThickness = 0.025f; // meters (world-space line)
    float culledLineThickness = 0.025f; // meters (world-space line)
    float lightLineThickness = 0.025f; // meters (world-space line)
    glm::vec4 aabbLineColor         = {0.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 colliderLineColor     = {0.0f, 0.88f, 0.88f, 1.0f};
    glm::vec4 meshLineColor         = {0.66f, 0.66f, 0.66f, 1.0f};
    glm::vec4 meshLineColorSelected = {1.0f, 0.66f, 0.0f, 1.0f};
    glm::vec4 cameraLineColor       = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 cameraLineColorSelected = {1.0f, 0.7, 0.18f, 1.0f};
    glm::vec4 culledLineColor       = {1.0f, 0.0f, 0.0f, 1.0f};
    float splineLineThickness = 0.025f; // meters (world-space line)
    glm::vec4 splineLineColor         = {0.15f, 0.75f, 0.85f, 1.0f};
    glm::vec4 splineLineColorSelected = {1.0f, 0.75f, 0.2f, 1.0f};

    // ctx.prefs.culledLineThickness
    void load();
    void save();

    void addRecentProject(const std::string &path, const std::string &name, const std::string &cardImage = "");
    void removeRecentProject(const std::string &path);

    void applyKeymapPreset();
    Input::Keymap getCurrentKeymapPreset() const;
  };
}
