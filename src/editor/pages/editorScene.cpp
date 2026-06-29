/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "pyrite64_version.h"
#include "editorScene.h"

#include "IconsMaterialDesignIcons.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "../actions.h"
#include "../undoRedo.h"
#include "../canvasHistory.h"
#include "../../context.h"
#include "../../project/assets/emitter.h"

#define IMVIEWGUIZMO_IMPLEMENTATION 1
#include "ImGuizmo.h"
#include "ImViewGuizmo.h"
#include "../../utils/logger.h"
#include "../../utils/ringBuffer.h"
#include "../../utils/updater.h"
#include "../imgui/notification.h"
#include "../imgui/theme.h"
#include "parts/assets/modelEditor.h"

namespace
{
  constinit bool preferencesOpen{false};
  constinit bool projectSettingsOpen{false};
  constinit bool needsSanityCheck{false};
  constinit Utils::RingBuffer<double, 16> fpsRingBuffer{};
}

Editor::Scene::Scene()
{
  Editor::Actions::registerAction(Editor::Actions::Type::OPEN_NODE_GRAPH, [this](const std::string& asset)
  {
    printf("OPEN_NODE_GRAPH action called with asset: %s\n", asset.c_str());
    if(!ctx.project)return false;
    auto assetEntry = ctx.project->getAssets().getEntryByUUID(std::stoull(asset));
    if(assetEntry) {
      nodeEditors.push_back(std::make_unique<NodeEditor>(assetEntry->getUUID()));
      return true;
    }
    return false;
  });

  Editor::Actions::registerAction(Editor::Actions::Type::OPEN_CANVAS, [this](const std::string& asset)
  {
    if(!ctx.project)return false;
    if (asset == "auto") {
      // Re-open last canvas, or fall back to first available
      if (ctx.openedCanvasUUID != 0) {
        openCanvas(ctx.openedCanvasUUID);
      } else {
        const auto& canvases = ctx.project->getAssets().getTypeEntries(Project::FileType::CANVAS);
        if (!canvases.empty()) openCanvas(canvases.front().getUUID());
      }
      return true;
    }
    openCanvas(std::stoull(asset));
    return true;
  });

  Editor::Actions::registerAction(Editor::Actions::Type::CLOSE_CANVAS, [this](const std::string&)
  {
    closeCanvas();
    return true;
  });

  needsSanityCheck = true;

  loadSession();
  if(viewports.empty()) addViewport();
}

void Editor::Scene::addViewport()
{
  viewports.push_back(std::make_shared<Viewport3D>(nextViewportWinId));
  ++nextViewportWinId;
}

Editor::Scene::~Scene()
{
  // The active project's windows are persisted when it is torn down (see persistOpenWindows).
  Editor::Actions::registerAction(Editor::Actions::Type::OPEN_NODE_GRAPH, nullptr);
  Editor::Actions::registerAction(Editor::Actions::Type::OPEN_CANVAS, nullptr);
  Editor::Actions::registerAction(Editor::Actions::Type::CLOSE_CANVAS, nullptr);
}

void Editor::Scene::loadSession()
{
  try
  {
    auto json = Utils::JSON::loadFile(Utils::Proc::getAppDataPath() / "editorScene.json");
    if(json.contains("projects")) {
      for(const auto& [path, w] : json["projects"].items()) {
        WindowSet ws{};
        if(w.contains("winModels")) for(const auto& u : w["winModels"]) ws.models.push_back(u.get<uint64_t>());
        if(w.contains("winGraphs")) for(const auto& u : w["winGraphs"]) ws.graphs.push_back(u.get<uint64_t>());
        sessionWindows[path] = std::move(ws);
      }
    }
    if(json.contains("viewports") && json["viewports"].is_array()) {
      for(const auto& v : json["viewports"]) {
        uint32_t id = v.value("winId", nextViewportWinId);
        auto vp = std::make_shared<Viewport3D>(id);
        vp->loadState(v);
        viewports.push_back(std::move(vp));
        nextViewportWinId = std::max(nextViewportWinId, id + 1);
      }
    }
    if(json.contains("openedCanvasUUID")) {
      uint64_t uuid = json["openedCanvasUUID"].get<uint64_t>();
      if(uuid != 0) {
        pendingRestoreCanvasUUID = uuid;
        assetsBrowser.forceTab(AssetsBrowser::TAB_CANVAS);
      }
    }
  } catch(const std::exception& e) {}
}

void Editor::Scene::saveSession()
{
  nlohmann::json conf{};
  conf["projects"] = nlohmann::json::object();
  for(const auto& [path, ws] : sessionWindows) {
    conf["projects"][path] = { {"winModels", ws.models}, {"winGraphs", ws.graphs} };
  }
  conf["viewports"] = nlohmann::json::array();
  for(const auto& vp : viewports) {
    conf["viewports"].push_back(vp->saveState());
  }
  conf["openedCanvasUUID"] = openedCanvas ? ctx.openedCanvasUUID : 0ULL;

  Utils::FS::saveTextFile(Utils::Proc::getAppDataPath() / "editorScene.json", conf.dump(2));
}

void Editor::Scene::openCanvas(uint64_t assetUUID)
{
  auto* entry = ctx.project->getAssets().getEntryByUUID(assetUUID);
  if (!entry || !entry->canvas) return;
  if (openedCanvas) {
    openedCanvas->save();
    ctx.project->getAssets().noteFileSaved(openedCanvas->path); // our own save; don't reload it
  }
  openedCanvas = entry->canvas;
  ctx.openedCanvasUUID = assetUUID;
  CanvasHistory::clear();
  canvasJustOpened = true;
}

void Editor::Scene::closeCanvas()
{
  if (openedCanvas) {
    openedCanvas->save();
    ctx.project->getAssets().noteFileSaved(openedCanvas->path); // our own save; don't reload it
    openedCanvas = nullptr;
  }
  // Keep ctx.openedCanvasUUID so "auto" can re-open it later
}

void Editor::Scene::persistOpenWindows()
{
  if(!ctx.project)return;
  WindowSet ws{};
  for(const auto& [assetUUID, _] : modelEditors) ws.models.push_back(assetUUID);
  for(const auto& nodeEditor : nodeEditors) {
    if(nodeEditor && nodeEditor->getAssetUUID() != 0) ws.graphs.push_back(nodeEditor->getAssetUUID());
  }
  sessionWindows[ctx.project->getPath()] = std::move(ws);
  saveSession();
}

void Editor::Scene::closeAllEditors()
{
  nodeEditors.clear();
  modelEditors.clear();
  pendingNodeEditorCloseUUID = 0;
  pendingNodeEditorClosePopup = false;
}

void Editor::Scene::onProjectClosing()
{
  persistOpenWindows();
  closeAllEditors();
  restoredForProject.clear();
}

void Editor::Scene::restoreWindows()
{
  auto it = sessionWindows.find(ctx.project->getPath());
  if(it == sessionWindows.end())return;
  for(auto uuid : it->second.models) {
    if(ctx.project->getAssets().getEntryByUUID(uuid)) openModelEditor(uuid);
  }
  for(auto uuid : it->second.graphs) {
    if(ctx.project->getAssets().getEntryByUUID(uuid)) {
      nodeEditors.push_back(std::make_shared<NodeEditor>(uuid));
    }
  }
}

void Editor::Scene::openModelEditor(uint64_t assetUUID)
{
  auto it = modelEditors.find(assetUUID);
  if(it != modelEditors.end()) {
    it->second->focus();
  } else {
    modelEditors[assetUUID] = std::make_unique<ModelEditor>(assetUUID);
  }
}

void Editor::Scene::draw()
{
  if(pendingRestoreCanvasUUID && ctx.project) {
    openCanvas(pendingRestoreCanvasUUID);
    pendingRestoreCanvasUUID = 0;
  }

  // Keep prefab instance subobjects in sync with their prefab definitions so edits
  // to a prefab (structure + transform) propagate live to all placed instances.
  // Done once at the top of the frame, before any scene-graph/viewport iteration.
  if(ctx.project && !ctx.isBuildOrRunning()) {
    if(auto loadedScene = ctx.project->getScenes().getLoadedScene()) {
      loadedScene->reconcilePrefabInstances();
    }
  }

  // Safe point to destroy viewports closed last frame: their render has completed.
  viewportsPendingClose.clear();

  // On a project switch, close the previous project's windows and restore this one's.
  if(ctx.project && ctx.project->getPath() != restoredForProject) {
    closeAllEditors();
    restoredForProject = ctx.project->getPath();
    restoreWindows();
  }

  float HEIGHT_TOP_BAR = 28_px;
  float HEIGHT_STATUS_BAR = 24_px;

  ImViewGuizmo::BeginFrame();
  ImGuizmo::BeginFrame();

  auto &io = ImGui::GetIO();
  auto viewport = ImGui::GetMainViewport();

  bool isRunning = ctx.isBuildOrRunning();

  ImGui::SetNextWindowPos({0, HEIGHT_TOP_BAR});
  ImGui::SetNextWindowSize({
    viewport->WorkSize.x,
    viewport->WorkSize.y - HEIGHT_TOP_BAR - HEIGHT_STATUS_BAR,
  });
  ImGui::SetNextWindowViewport(viewport->ID);

  ImGuiWindowFlags host_window_flags = 0;
  host_window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;
  host_window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});
  ImGui::Begin("MAIN_DOCK", NULL, host_window_flags);
  ImGui::PopStyleVar(3);

  auto dockSpaceID = ImGui::GetID("DockSpace");
  auto dockSpace = ImGui::DockBuilderGetNode(dockSpaceID);

  dockSpaceID = ImGui::DockSpace(dockSpaceID, ImVec2(0.0f, 0.0f), 0, 0);
  ImGui::End();

  if(!dockSpace)
  {
    ImGui::DockBuilderRemoveNode(dockSpaceID); // Clear out existing layout
    ImGui::DockBuilderAddNode(dockSpaceID); // Add empty node
    ImGui::DockBuilderSetNodeSize(dockSpaceID, ImGui::GetMainViewport()->Size);

    dockLeftID = ImGui::DockBuilderSplitNode(dockSpaceID, ImGuiDir_Left, 0.15f, nullptr, &dockSpaceID);
    dockRightID = ImGui::DockBuilderSplitNode(dockSpaceID, ImGuiDir_Right, 0.25f, nullptr, &dockSpaceID);
    dockBottomID = ImGui::DockBuilderSplitNode(dockSpaceID, ImGuiDir_Down, 0.25f, nullptr, &dockSpaceID);

    // Center
    ImGui::DockBuilderDockWindow("3D-Viewport", dockSpaceID);
    ImGui::DockBuilderDockWindow("Canvas",      dockSpaceID);
    ImGui::DockBuilderDockWindow("Particles",   dockSpaceID);

    // Left
    ImGui::DockBuilderDockWindow("Scene",     dockLeftID);
    ImGui::DockBuilderDockWindow("Graph",     dockLeftID);
    ImGui::DockBuilderDockWindow("Layers",    dockLeftID);
    ImGui::DockBuilderDockWindow("Timeline",  dockBottomID);

    // Right
    ImGui::DockBuilderDockWindow("Asset", dockRightID);
    ImGui::DockBuilderDockWindow("Object", dockRightID);
    ImGui::DockBuilderDockWindow("Model", dockRightID);

    // Bottom
    ImGui::DockBuilderDockWindow("Files", dockBottomID);
    ImGui::DockBuilderDockWindow("Log", dockBottomID);
    ImGui::DockBuilderDockWindow("Build", dockBottomID);
    ImGui::DockBuilderDockWindow("ROM", dockBottomID);

    ImGui::DockBuilderFinish(dockSpaceID);
  }

  CanvasHistory::begin(openedCanvas.get());

  const ImGuiCond dockCond = canvasJustOpened ? ImGuiCond_Always : ImGuiCond_Appearing;

  // The viewport-area windows (3D-Viewports / Canvas / Particles) share ONE dock node, so ImGui
  // shows them as tabs and only one is in the foreground at a time. Each draw() is gated on
  // ImGui::Begin()'s return: a *background* tab must NOT run its per-frame input/gizmo logic,
  // because two of them processing the mouse + ImGuizmo in the same frame corrupts the active
  // one (objects move wrong, the gizmo fights the click-drag). Only the foreground tab runs.
  hoveredViewport = nullptr;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2_px, 2_px));

  // 3D viewports (main #264): a dynamic list of dockable viewport windows. The first is titled
  // "3D-Viewport" and docks to the center node (see DockBuilder above); extras tab alongside.
  std::vector<size_t> viewportsToClose{};
  for(size_t i = 0; i < viewports.size(); ++i) {
    auto &vp = viewports[i];
    bool open = true;
    std::string title = vp->getWindowTitle();
    if(ImGui::Begin(title.c_str(), &open)) {
      // Right-clicking the docked tab offers opening another viewport.
      ImGuiWindow* win = ImGui::GetCurrentWindow();
      if(win->DockIsActive &&
         ImGui::IsMouseHoveringRect(win->DC.DockTabItemRect.Min, win->DC.DockTabItemRect.Max, false) &&
         ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("ViewportTabCtx");
      }
      if(ImGui::BeginPopup("ViewportTabCtx")) {
        if(ImGui::MenuItem(ICON_MDI_PLUS " New Viewport")) wantNewViewport = true;
        ImGui::EndPopup();
      }
      vp->draw();
      if(vp->isViewHovered()) hoveredViewport = vp;
    }
    ImGui::End();
    if(!open) viewportsToClose.push_back(i);
  }

  // Canvas — a tab only while a canvas is open; shares the center dock node, brought forward
  // the frame it opens.
  if (openedCanvas) {
    // Dock the Canvas as a tab alongside the primary 3D viewport (its current node),
    // not the root dockspace node. Forcing it to the root on each (re)open could
    // surface the re-opened canvas in the wrong/hidden spot — the bug where you
    // "couldn't get back into canvas mode" after switching to the scene.
    ImGuiID canvasDock = dockSpaceID;
    if (ImGuiWindow* vpWin = ImGui::FindWindowByName("3D-Viewport"))
      if (vpWin->DockId != 0) canvasDock = vpWin->DockId;
    ImGui::SetNextWindowDockID(canvasDock, dockCond);
    if (canvasJustOpened) ImGui::SetNextWindowFocus();
    if (ImGui::Begin("Canvas")) {
      canvasViewport.draw(*openedCanvas);
    }
    ImGui::End();
    // Consume the "just opened" flag HERE — right after the Canvas window draws.
    // openCanvas() is called later in the frame (from the Files browser), after this
    // window already drew, so the focus/dock must take effect on the NEXT frame's
    // draw of this window. Resetting it in the lower panel block (same frame as the
    // click) cleared it before this window ever saw it -> re-open never refocused.
    canvasJustOpened = false;
  }
  ImGui::PopStyleVar(1);

  // Apply viewport open/close/new/reset requests gathered above.
  bool viewportsChanged = !viewportsToClose.empty();
  for(auto it = viewportsToClose.rbegin(); it != viewportsToClose.rend(); ++it) {
    viewports[*it]->detach();
    viewportsPendingClose.push_back(std::move(viewports[*it]));
    viewports.erase(viewports.begin() + *it);
  }
  if(wantNewViewport) {
    wantNewViewport = false;
    addViewport();
    viewportsChanged = true;
  }
  if(wantResetLayout) {
    wantResetLayout = false;
    // Collapse to a single default viewport so the rebuilt layout has a center window.
    hoveredViewport = nullptr;
    for(auto &vp : viewports) { vp->detach(); viewportsPendingClose.push_back(std::move(vp)); }
    viewports.clear();
    nextViewportWinId = 0;
    addViewport();
    viewportsChanged = true;
    ImGui::DockBuilderRemoveNode(dockSpaceID);
  }
  if(viewportsChanged) saveSession();

  // The selected asset drives the emitter preview. When selection moves off an emitter,
  // persist its edits (the settings are edited live in the Asset inspector).
  uint64_t curEmitter = 0;
  if(ctx.selAssetUUID != 0) {
    auto *e = ctx.project->getAssets().getEntryByUUID(ctx.selAssetUUID);
    if(e && e->type == Project::FileType::EMITTER && e->emitter) curEmitter = ctx.selAssetUUID;
  }
  if(curEmitter != editingEmitterUUID) {
    if(editingEmitterUUID != 0) {
      auto *prev = ctx.project->getAssets().getEntryByUUID(editingEmitterUUID);
      if(prev && prev->emitter) {
        prev->emitter->save();
        ctx.project->getAssets().noteFileSaved(prev->path); // don't let our own save trigger a watch reload
      }
    }
    editingEmitterUUID = curEmitter;
    if(curEmitter != 0) focusParticlesTab = true; // a new emitter was selected -> bring its tab forward
  }
  previewEmitterUUID = curEmitter;

  // Particles — a tab only while an emitter is selected. Same dock node as the viewport, gated
  // on Begin() so it only simulates/renders + handles input when it's the foreground tab.
  if(previewEmitterUUID != 0) {
    auto *entry = ctx.project->getAssets().getEntryByUUID(previewEmitterUUID);
    if(entry && entry->emitter) {
      // Dock as a tab alongside the primary 3D viewport (its current node), not the
      // root dockspace node — same as the Canvas window, so re-opening reliably
      // surfaces it beside the viewport instead of in a misplaced/hidden spot.
      ImGuiID particlesDock = dockSpaceID;
      if (ImGuiWindow* vpWin = ImGui::FindWindowByName("3D-Viewport"))
        if (vpWin->DockId != 0) particlesDock = vpWin->DockId;
      ImGui::SetNextWindowDockID(particlesDock, focusParticlesTab ? ImGuiCond_Always : ImGuiCond_Appearing);
      if(focusParticlesTab) { ImGui::SetNextWindowFocus(); focusParticlesTab = false; }
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2_px, 2_px));
      if (ImGui::Begin("Particles")) {
        emitterPreview.draw(*entry->emitter, previewEmitterUUID);
      }
      ImGui::End();
      ImGui::PopStyleVar(1);
    } else {
      focusParticlesTab = false;
    }
  }

  std::vector<uint32_t> delIndices{};
  for(uint32_t i = 0; i < nodeEditors.size(); ++i) {
    auto &nodeEditor = nodeEditors[i];
    if (!nodeEditor->draw(dockSpaceID)) {
      if (nodeEditor->isDirty()) {
        pendingNodeEditorCloseUUID = nodeEditor->getAssetUUID();
        pendingNodeEditorClosePopup = true;
      } else {
        delIndices.push_back(i);
      }
    }
  }
  // Remove closed editors
  for(int32_t i = (int32_t)delIndices.size() - 1; i >= 0; --i) {
    nodeEditors.erase(nodeEditors.begin() + delIndices[i]);
  }

  if (pendingNodeEditorClosePopup) {
    ImGui::OpenPopup("Unsaved Node Graph");
    pendingNodeEditorClosePopup = false;
  }

  if (ImGui::BeginPopupModal("Unsaved Node Graph", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    auto itEditor = std::find_if(nodeEditors.begin(), nodeEditors.end(), [&](const std::shared_ptr<NodeEditor> &editor) {
      return editor && editor->getAssetUUID() == pendingNodeEditorCloseUUID;
    });

    if (itEditor == nodeEditors.end()) {
      pendingNodeEditorCloseUUID = 0;
      ImGui::CloseCurrentPopup();
    } else {
      auto &editor = *itEditor;
      ImGui::Text("The node graph '%s' has unsaved changes.", editor->getName().c_str());
      ImGui::Spacing();
      if (ImGui::Button("Save", {100_px, 0})) {
        editor->save();
        nodeEditors.erase(itEditor);
        pendingNodeEditorCloseUUID = 0;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Discard", {100_px, 0})) {
        editor->discardUnsavedChanges();
        nodeEditors.erase(itEditor);
        pendingNodeEditorCloseUUID = 0;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", {100_px, 0})) {
        pendingNodeEditorCloseUUID = 0;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

  std::vector<uint64_t> delUUIDs{};
  for(auto &[uuid, editor] : modelEditors) {
    if (!editor->draw(dockSpaceID)) {
      delUUIDs.push_back(uuid);
    }
  }
  for(auto &uuid : delUUIDs)modelEditors.erase(uuid);

  if (!openedCanvas) {
    ImGui::Begin("Object");
      objectInspector.draw();
    ImGui::End();
  }

  ImGui::Begin("Asset");
    if (openedCanvas) {
      // In canvas mode the Asset window IS the canvas inspector: settings + variables + events.
      if (ImGui::Button("Close Canvas")) {
        closeCanvas();
        assetsBrowser.setActiveTab(AssetsBrowser::TAB_ASSETS); // leave prevActiveTab=4 so detection fires
      }
      ImGui::Separator();
      canvasSettings.draw(*openedCanvas);
      canvasVariables.draw(*openedCanvas);
      canvasEvents.draw(*openedCanvas);
    } else {
      assetInspector.draw();
    }
  ImGui::End();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2_px, 2_px));
  ImGui::Begin("Files");
  ImGui::PopStyleVar();
    assetsBrowser.draw();
  ImGui::End();

  if (openedCanvas)
  {
    // Canvas editor mode. Canvas settings / variables / events now live in the Asset
    // window (above); the left dock just shows the element Graph. Timeline docks bottom.
    ImGui::SetNextWindowDockID(dockBottomID, dockCond);
    ImGui::Begin("Timeline");
      canvasTimeline.draw(*openedCanvas, canvasGraph.getSelectedUUID());
    ImGui::End();
    // Live preview: feed the timeline's per-element offsets to the viewport (applied next frame).
    canvasViewport.setAnimOverrides(canvasTimeline.getPreviewOverrides());

    ImGui::Begin("Graph");
      canvasGraph.draw(*openedCanvas);
    ImGui::End();

    // (canvasJustOpened is reset right after the Canvas window draws, above.)

    // Bidirectional selection sync: viewport click wins this frame, otherwise graph drives viewport
    if (canvasViewport.consumeSelectionChange())
      canvasGraph.setSelectedUUID(canvasViewport.getSelected());
    else
      canvasViewport.setSelected(canvasGraph.getSelectedUUID());

    ImGui::Begin("Object");
      canvasObjectInspector.draw(*openedCanvas, canvasGraph.getSelected(*openedCanvas));
    ImGui::End();
  }
  else if (ctx.project->getScenes().getLoadedScene())
  {
    ImGui::Begin("Graph");
      sceneGraph.draw();
    ImGui::End();

    ImGui::Begin("Scene");
      sceneInspector.draw();
    ImGui::End();

    ImGui::Begin("Layers");
      layerInspector.draw();
    ImGui::End();
  }

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2_px, 2_px));
  ImGui::Begin("Log");
  ImGui::PopStyleVar();
    logWindow.draw();
  ImGui::End();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2_px, 2_px));
  ImGui::Begin("Build");
  ImGui::PopStyleVar();
    logWindow.drawBuild();
  ImGui::End();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4_px, 4_px));
  ImGui::Begin("ROM");
  ImGui::PopStyleVar();
    memoryDashboard.draw();
  ImGui::End();

  if (preferencesOpen) {
    ImVec2 windowSize{500_px, 300_px};
    auto screenSize = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowPos({(screenSize.x - windowSize.x) / 2, (screenSize.y - windowSize.y) / 2}, ImGuiCond_Appearing, {0.0f, 0.0f});
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Appearing);

    // Thick borders
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0_px);
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    ImGui::Begin(ICON_MDI_COG " Preferences", &preferencesOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
    if (prefOverlay.draw()) {
      preferencesOpen = false;
    }
    ImGui::End();

    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(1);
  }

  if (projectSettingsOpen) {
    ImVec2 windowSize{600_px,400_px};
    auto screenSize = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowPos({(screenSize.x - windowSize.x) / 2, (screenSize.y - windowSize.y) / 2}, ImGuiCond_Appearing, {0.0f, 0.0f});
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Appearing);

    // Thick borders
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0_px);
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    ImGui::Begin(ICON_MDI_FILE_COG_OUTLINE " Project Settings", &projectSettingsOpen,
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    if (projectSettings.draw()) {
      projectSettingsOpen = false;
    }
    ImGui::End();

    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(1);
  }

  // Top bar
  ImGui::SetNextWindowPos({0,0}, ImGuiCond_Always);
  ImGui::SetNextWindowSize({io.DisplaySize.x, 4}, ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{8_px,6_px});
  if(ImGui::Begin("TOP_BAR", 0,
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoTitleBar
    | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking
  )) {
    if(ImGui::BeginMenuBar())
    {
      if(ImGui::BeginMenu("Project"))
      {
        if(ImGui::MenuItem(ICON_MDI_CONTENT_SAVE_OUTLINE " Save")) {
          ctx.project->save();
          save();
        }
        if(ImGui::MenuItem(ICON_MDI_FILE_COG_OUTLINE " Settings"))projectSettingsOpen = true;
        if(ImGui::MenuItem(ICON_MDI_CLOSE " Close"))Actions::call(Actions::Type::PROJECT_CLOSE);
        ImGui::EndMenu();
      }

      // Edit Menu with undo/redo functionality including description
      if(ImGui::BeginMenu("Edit"))
      {
        std::string undoText = ICON_MDI_UNDO " Undo";
        std::string redoText = ICON_MDI_REDO " Redo";
        bool canUndo, canRedo;

        if (openedCanvas) {
          canUndo = CanvasHistory::canUndo();
          canRedo = CanvasHistory::canRedo();
          if (canUndo) undoText += " (" + CanvasHistory::getUndoDescription() + ")";
          if (canRedo) redoText += " (" + CanvasHistory::getRedoDescription() + ")";
        } else {
          auto& history = UndoRedo::getHistory();
          canUndo = history.canUndo();
          canRedo = history.canRedo();
          if (canUndo) undoText += " (" + history.getUndoDescription() + ")";
          if (canRedo) redoText += " (" + history.getRedoDescription() + ")";
        }

        if(ImGui::MenuItem(undoText.c_str(), "Ctrl+Z", false, canUndo)) {
          if (openedCanvas) CanvasHistory::undo();
          else UndoRedo::getHistory().undo();
        }
        if(ImGui::MenuItem(redoText.c_str(), "Ctrl+Y", false, canRedo)) {
          if (openedCanvas) CanvasHistory::redo();
          else UndoRedo::getHistory().redo();
        }

        if(ImGui::MenuItem(ICON_MDI_COG " Preferences", "Ctrl+."))preferencesOpen = true;

        ImGui::EndMenu();
      }

      if(ImGui::BeginMenu("Build"))
      {
        if(ImGui::MenuItem(ICON_MDI_HAMMER " Build"))Actions::call(Actions::Type::PROJECT_BUILD);
        if(ImGui::MenuItem(ICON_MDI_PLAY " Build & Run"))Actions::call(Actions::Type::PROJECT_BUILD, "run");
        if(ImGui::MenuItem("Clean"))Actions::call(Actions::Type::PROJECT_CLEAN);
        ImGui::EndMenu();
      }

      if(ImGui::BeginMenu("View"))
      {
        if(ImGui::MenuItem(ICON_MDI_PLUS " New Viewport")) wantNewViewport = true;
        ImGui::Separator();
        if(ImGui::MenuItem(ICON_MDI_MAGNIFY_PLUS " Zoom In")) {
          ImGui::Theme::changeZoom(+1);
        }
        if(ImGui::MenuItem(ICON_MDI_MAGNIFY_MINUS "Zoom Out")) {
          ImGui::Theme::changeZoom(-1);
        }
        if(ImGui::BeginMenu(ICON_MDI_PALETTE " Theme"))
        {
          for(const auto &theme : ImGui::Theme::getThemes())
          {
            bool selected = theme.id == ImGui::Theme::getCurrentTheme();
            if(ImGui::MenuItem(theme.name.c_str(), nullptr, selected))
            {
              ImGui::Theme::setTheme(theme.id);
              ctx.prefs.themeName = theme.id;
              ctx.prefs.save();
            }
          }
          ImGui::EndMenu();
        }
        if(ImGui::MenuItem("Reset Layout"))wantResetLayout = true;
        ImGui::EndMenu();
      }

      ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 40_px);

      const char* tooltip{};
      ImGui::PushFont(nullptr, 20.0_px);
      if(isRunning){
        ImGui::BeginDisabled();
        ImGui::MenuItem(ICON_MDI_STOP);
        ImGui::EndDisabled();
      } else {
        ImGui::PushStyleColor(ImGuiCol_Text, {0.6f, 0.85f, 0.6f, 1.0f});
        if(ImGui::MenuItem(ICON_MDI_PLAY))Actions::call(Actions::Type::PROJECT_BUILD, "run");
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))tooltip = "Run (F12)";
        ImGui::PopStyleColor();
      }

      ImGui::PopFont();

      if(tooltip)ImGui::SetTooltip("%s", tooltip);

      ImGui::EndMenuBar();
    }
    ImGui::End();
  }
  ImGui::PopStyleVar();

  // Bottom Status bar
  ImGui::SetNextWindowPos({0, io.DisplaySize.y - HEIGHT_STATUS_BAR}, ImGuiCond_Always, {0.0f, 0.0f});
  ImGui::SetNextWindowSize({io.DisplaySize.x, HEIGHT_STATUS_BAR}, ImGuiCond_Always);
  ImGui::Begin("STATUS_BAR", 0,
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar
    | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking
  );

  fpsRingBuffer.push((double)ctx.timeCpuSelf / 1000.0 / 1000.0);

  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5_px);
  ImGui::PushFont(ImGui::Theme::getFontMono(), 16_px);
  ImVec4 textCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);
  ImVec4 perfColor{textCol.x, textCol.y, textCol.z, 0.55f};
  if (io.Framerate < 45) perfColor = {0.85f, 0.30f, 0.30f, 1.0f};
  if (openedCanvas) {
    ImGui::TextColored(perfColor, "%d FPS | Canvas: %d/%d %s | CPU: %.2fms",
      (int)roundf(io.Framerate),
      CanvasHistory::getUndoCount(),
      CanvasHistory::getRedoCount(),
      Utils::byteSize(CanvasHistory::getMemoryUsage()).c_str(),
      fpsRingBuffer.average()
    );
  } else {
    ImGui::TextColored(perfColor, "%d FPS | History: %d/%d %s | CPU: %.2fms",
      (int)roundf(io.Framerate),
      UndoRedo::getHistory().getUndoCount(),
      UndoRedo::getHistory().getRedoCount(),
      Utils::byteSize(UndoRedo::getHistory().getMemoryUsage()).c_str(),
      fpsRingBuffer.average()
    );
  }

  ImGui::SameLine();
  auto posX = io.DisplaySize.x - 12_px;

  if(!ctx.newerVersion.empty()) {
    ImGui::PopFont();

    auto txt = ICON_MDI_DOWNLOAD " Update Available: " + ctx.newerVersion;
    posX -= ImGui::CalcTextSize(txt.c_str()).x + 4;
    auto posY = ImGui::GetCursorPosY();;
    ImGui::SetCursorPos({posX, posY - 2});

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {5_px, 2_px});
    ImGui::PushStyleColor(ImGuiCol_Button, {0.5f, 0.8f, 0.0f, 0.9f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.5f, 0.8f, 0.0f, 0.75f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, {1.0f, 0.5f, 0.0f, 0.6f});
    ImGui::PushStyleColor(ImGuiCol_Text, {0.0f, 0.0f, 0.0f, 1.0f});

    if(ImGui::Button(txt.c_str(), {0,0})) {
      Utils::Updater::doUpdate(ctx.newerVersion);
    }

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(1);

    ImGui::SetCursorPosY(posY);
    ImGui::PushFont(ImGui::Theme::getFontMono());
    posX -= 8_px;
  }

  perfColor = {textCol.x, textCol.y, textCol.z, 0.45f};
  std::string txtInfo = "v" PYRITE_VERSION;
  #ifndef NDEBUG
    perfColor = {textCol.x, textCol.y, textCol.z, 0.65f};
    txtInfo += " [DEBUG]";
  #endif

  ImGui::SetCursorPosX(posX - ImGui::CalcTextSize(txtInfo.c_str()).x);
  ImGui::TextColored(perfColor, "%s", txtInfo.c_str());

  ImGui::PopFont();
  ImGui::End();

  // Global keyboard shortcuts
  if (!ImGui::GetIO().WantTextInput) {
    bool isCtrl = ImGui::GetIO().KeyCtrl;
    bool isShift = ImGui::GetIO().KeyShift;
    
    // Undo: Ctrl+Z
    if (isCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
      if (openedCanvas) CanvasHistory::undo();
      else UndoRedo::getHistory().undo();
    }

    // Redo: Ctrl+Y
    if (isCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
      if (openedCanvas) CanvasHistory::redo();
      else UndoRedo::getHistory().redo();
    }

    // Save: Ctrl+S
    if (isCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
      ctx.project->save();
      save();
    }

    // Align focused object to the editor camera: Ctrl+Shift+F (uses the hovered viewport)
    if (isCtrl && isShift && ImGui::IsKeyPressed(ImGuiKey_F)) {
      auto vp = hoveredViewport ? hoveredViewport : (viewports.empty() ? nullptr : viewports.front());
      if (vp) vp->alignFocusedObjectToCamera();
    }

    // Preferences
    if (isCtrl && ImGui::IsKeyPressed(ImGuiKey_Period))preferencesOpen = true;
  }

  if(needsSanityCheck)
  {
    // check for duplicated asset UUIDs
    auto &assets = ctx.project->getAssets().getEntries();
    std::unordered_map<uint64_t, const Project::AssetManagerEntry*> uuids{};
    for (const auto &assetTypes : assets)
    {
      for (const auto &asset : assetTypes)
      {
        auto existing = uuids.find(asset.getUUID());
        if (existing != uuids.end()) {
          auto msg = "Duplicate UUID found: " + std::to_string(asset.getUUID()) + "\nAsset: " + asset.name
             + "\nWith: " + existing->second->name;
          if(ctx.window) {
            Editor::Noti::add(Noti::ERROR, msg);
          } else {
            Utils::Logger::log(msg, Utils::Logger::LEVEL_ERROR);
          }
        } else {
          uuids[asset.getUUID()] = &asset;
        }
      }
    }
    needsSanityCheck = false;
  }

  CanvasHistory::end();
}

void Editor::Scene::save()
{
  if (openedCanvas) openedCanvas->save();
  for(auto &nodeEditor : nodeEditors) {
    nodeEditor->save();
  }
  if(editingEmitterUUID != 0) {
    auto *e = ctx.project->getAssets().getEntryByUUID(editingEmitterUUID);
    if(e && e->emitter) {
      e->emitter->save();
      ctx.project->getAssets().noteFileSaved(e->path);
    }
  }
  persistOpenWindows();
  UndoRedo::getHistory().markSaved();
}
