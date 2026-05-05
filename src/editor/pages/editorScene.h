/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include "parts/assetInspector.h"
#include "parts/assetsBrowser.h"
#include "parts/canvasGraph.h"
#include "parts/canvasObjectInspector.h"
#include "parts/canvasSettings.h"
#include "parts/canvasVariables.h"
#include "parts/canvasViewport.h"
#include "parts/layerInspector.h"
#include "parts/logWindow.h"
#include "parts/memoryDashboard.h"
#include "parts/nodeEditor.h"
#include "parts/objectInspector.h"
#include "parts/preferenceOverlay.h"
#include "parts/projectSettings.h"
#include "parts/sceneGraph.h"
#include "parts/sceneInspector.h"
#include "parts/viewport3D.h"

namespace Editor
{
  class ModelEditor;

  class Scene
  {
    private:
      Viewport3D viewport3d{};

      // Canvas editor panels (active when a canvas is open)
      std::shared_ptr<Project::Canvas> openedCanvas{};
      CanvasViewport      canvasViewport{};
      CanvasVariables     canvasVariables{};
      CanvasGraph         canvasGraph{};
      CanvasSettings      canvasSettings{};
      CanvasObjectInspector canvasObjectInspector{};

      // Editors
      std::vector<std::shared_ptr<NodeEditor>> nodeEditors{};
      std::map<uint64_t, std::shared_ptr<ModelEditor>> modelEditors{};
      PreferenceOverlay prefOverlay{};
      ProjectSettings projectSettings{};
      AssetsBrowser assetsBrowser{};
      AssetInspector assetInspector{};
      SceneInspector sceneInspector{};
      LayerInspector layerInspector{};
      ObjectInspector objectInspector{};
      LogWindow logWindow{};
      MemoryDashboard memoryDashboard{};
      SceneGraph sceneGraph{};

      uint64_t pendingRestoreCanvasUUID{0};
      bool dockSpaceInit{false};
      bool canvasJustOpened{false};
      ImGuiID dockLeftID{};
      ImGuiID dockRightID{};
      ImGuiID dockBottomID{};

      uint64_t pendingNodeEditorCloseUUID{0};
      bool pendingNodeEditorClosePopup{false};

    public:
      Scene();
      ~Scene();

      void openModelEditor(uint64_t assetUUID);
      void openCanvas(uint64_t assetUUID);
      void closeCanvas();

      void draw();
      void save();
  };
}
