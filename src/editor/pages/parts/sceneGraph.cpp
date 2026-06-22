/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "sceneGraph.h"

#include <algorithm>
#include <string>
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "../../../context.h"
#include "../../imgui/helper.h"
#include "IconsMaterialDesignIcons.h"
#include "imgui_internal.h"
#include "../../undoRedo.h"
#include "../../selectionUtils.h"

namespace
{
  Project::Object* deleteObj{nullptr};
  bool deleteSelection{false};
  uint32_t renameObjectUUID{0};
  std::string renameBuffer{};
  bool startingRename{false};

  struct DragDropTask {
    uint32_t sourceUUID{0};
    uint32_t targetUUID{0};
    bool isInsert{false};
  };

  DragDropTask dragDropTask{};

  struct PrefabDropTask {
    uint64_t prefabUUID{0};
    uint32_t targetNodeUUID{0};
  };

  PrefabDropTask prefabDropTask{};

  // Deferred structural edits to a prefab template (add/remove subobject). These mutate the
  // scene graph via reconcile, so they must run after the tree iteration, not during it.
  struct PrefabEditTask {
    enum class Type { NONE, ADD_CHILD, REMOVE_NODE };
    Type type{Type::NONE};
    uint32_t nodeUUID{0};
  };

  PrefabEditTask prefabEditTask{};

  /**
   * Builds the icon prefix shown before the node name in the scene tree.
   *
   * The prefix may contain the prefab marker plus either the first component icon or a fallback icon.
   *
   * @param obj Scene object whose tree-row icons will be generated.
   * @return Concatenated icon string displayed before the node name.
   */
  std::string getNodeIcons(const Project::Object &obj)
  {
    std::string prefix{};

    // Only an actual prefab INSTANCE (placed instance or nested-prefab reference) gets the
    // prefab icon — NOT a plain subobject that merely carries uuidPrefab=owner for reconcile.
    // Mark with a pencil when THIS prefab is in edit mode so it's clear where edits land.
    auto loadedScene = ctx.project->getScenes().getLoadedScene();
    if(loadedScene && loadedScene->isPrefabInstanceRoot(obj)) {
      prefix += ICON_MDI_PACKAGE_VARIANT_CLOSED " ";
      if(obj.isPrefabEdit) prefix += ICON_MDI_PENCIL " ";
    }

    bool gotComponentIcon = false;
    // The object has components
    if (!obj.components.empty()) {
      // Reuse the first component icon so the node hints at its main role
      const Project::Component::Entry &compEntry = obj.components.front();

      // Is a valid component
      if (compEntry.id >= 0 && (size_t)compEntry.id < Project::Component::TABLE.size()) {
        const Project::Component::CompInfo &def = Project::Component::TABLE[compEntry.id];

        // The component has a custom icon --> Use it
        if (def.icon) {
          prefix += def.icon;
          gotComponentIcon = true;
        }
      }
    }

    // Couldn't get a component icon --> Fall back to a root icon or a generic cube icon
    if (!gotComponentIcon) {
      prefix += (obj.parent == nullptr)
        ? ICON_MDI_MOVIE_OPEN_OUTLINE " "
        : ICON_MDI_CUBE_OUTLINE " ";
    }

    return prefix;
  }

  /**
   * Computes the horizontal area reserved for the controls at the right side of a row.
   *
   * @return Width that must remain free at the right side of the row.
   */
  float calcRightControlAreaWidth()
  {
    const int iconAmount = 2;
    const ImGuiStyle& style = ImGui::GetStyle();

    // Sum the width of all the buttons
    return ImGui::CalcTextSize(ICON_MDI_CURSOR_DEFAULT).x * iconAmount
      // Sum the width of margins between buttons
      + style.ItemInnerSpacing.x * (iconAmount - 1)
      // Keep a small buffer against the window edge
      + style.WindowPadding.x
      // Add the width of the scrollbar if not present
      + (ImGui::GetCurrentWindow()->ScrollbarY ? 0 : style.ScrollbarSize);
  }

  /**
   * Clears the current inline renaming state.
   */
  void clearRenaming()
  {
    renameObjectUUID = 0;
    renameBuffer.clear();
    startingRename = false;
  }

  /**
   * Starts inline renaming for an object.
   *
   * @param objectUUID UUID of the object to rename
   * @param objectName Current name
   */
  void startRenaming(uint32_t objectUUID)
  {
    // Get the scene to look for the object
    auto scene = ctx.project->getScenes().getLoadedScene();
    if (!scene) return;

    // Can find object with such UUID --> Start renaming
    if (const std::shared_ptr<Project::Object> theObject = scene->getObjectByUUID(objectUUID)) {
      renameObjectUUID = objectUUID;
      renameBuffer = theObject->name;
      startingRename = true;
    // Cannot find object with such UUID (selection may have gone stale between frames) --> Cancel renaming
    } else {
      clearRenaming();
    }
  }

  bool DrawDropTarget(uint32_t& dragDropTarget, uint32_t uuid, float thickness = 2.0f, float hitHeight = 8.0f)
  {
    // Only show when drag-drop is active
    if (!ImGui::IsDragDropActive())
      return false;

    bool res = false;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
    float fullWidth = ImGui::GetContentRegionAvail().x;

    // Compute overlay position
    ImVec2 overlayStart{
      cursorScreen.x - 4_px,
      cursorScreen.y - (hitHeight / 2) + 3_px
    };
    ImVec2 overlayEnd = ImVec2(cursorScreen.x + fullWidth, cursorScreen.y + hitHeight);

    // Push a dummy cursor to draw hit zone *without affecting layout*
    ImGui::SetCursorScreenPos(overlayStart);
    ImGui::PushID(("drop_overlay_" + std::to_string(uuid)).c_str());
    ImGui::InvisibleButton("##dropzone", ImVec2(fullWidth, hitHeight));
    bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (hovered) {
      drawList->AddLine(
          ImVec2(overlayStart.x, overlayStart.y),
          ImVec2(overlayEnd.x, overlayStart.y),
          ImGui::GetColorU32(ImGuiCol_DragDropTarget),
          thickness
      );
    }

    ImGui::PushStyleColor(ImGuiCol_DragDropTarget, ImVec4(0,0,0,0));
    // Accept drag payload
    if (ImGui::BeginDragDropTarget())
    {
      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OBJECT"))
      {
        dragDropTarget = *((uint32_t*)payload->Data);
        res = true;
      }
      ImGui::EndDragDropTarget();
    }
    ImGui::PopStyleColor();

    ImGui::PopID();

    ImGui::SetCursorScreenPos(cursorScreen);
    return res;
  }

  /**
   * Draws an inline rename text field on top of a scene-graph node label.
   *
   * The edit is confirmed on Enter or when the field loses focus, and cancelled with Escape.
   *
   * @param obj The scene object currently being renamed.
  */
  void drawRenameInput(Project::Object &obj, const ImVec2 &nodeRectMin)
  {
    const ImVec2 oldCursorPos = ImGui::GetCursorPos();

    // Anchor input to the tree label position
    ImVec2 renamePos = nodeRectMin;
    const ImGuiStyle& style = ImGui::GetStyle();
    renamePos.x += ImGui::GetTreeNodeToLabelSpacing() / 2 - style.FramePadding.x + 2;
    renamePos.x += ImGui::CalcTextSize(getNodeIcons(obj).c_str()).x;
    renamePos.y -= 1;
    ImGui::SetCursorScreenPos(renamePos);

    // Clamp input width to the usable row space so it stays inside the window
    float rightLimit = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - calcRightControlAreaWidth() - style.FramePadding.x;
    float inputWidth = rightLimit - ImGui::GetCursorScreenPos().x;
    if (inputWidth < 1_px)
      inputWidth = 1_px;

    // Is the first frame --> Focus input
    if (startingRename) {
      ImGui::SetKeyboardFocusHere();
      startingRename = false;
    }

    // Place input and read value
    ImGui::SetNextItemWidth(inputWidth);
    bool confirmRename = ImGui::InputText(
      ("##Rename" + std::to_string(obj.uuid)).c_str(),
      &renameBuffer,
      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll
    );

    // Escape aborts rename
    bool cancelRename = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
    // Enter or losing focus commits name
    bool finishRename = confirmRename || ImGui::IsItemDeactivated();
    // Canceled --> Clear renaming
    if (cancelRename) {
      clearRenaming();
    // Finished renaming --> Commit name
    } else if (finishRename) {
      // Given new name --> Apply to object
      if (!renameBuffer.empty() && obj.name != renameBuffer) {
        auto sc = ctx.project->getScenes().getLoadedScene();
        if (sc && sc->isPrefabSubobject(obj)) {
          // A subobject's name is reconcile-driven (copied from its template), so a direct
          // rename would revert. Route it to the template node in the active edit scope.
          auto t = sc->resolveEditTarget(obj);
          if (t.parent) {
            t.parent->name = renameBuffer;
            obj.name = renameBuffer; // immediate feedback; reconcile keeps it in sync
            ctx.project->getAssets().markPrefabDirty(t.prefabUUID);
            Editor::UndoRedo::getHistory().markChanged("Rename Prefab Subobject");
          }
          // else: locked in the current scope -> ignore the rename
        } else {
          obj.name = renameBuffer;
          Editor::UndoRedo::getHistory().markChanged("Edit object name");
        }
      }
      clearRenaming();
    }

    ImGui::SetCursorPos(oldCursorPos);
  }

  void drawObjectNode(
    Project::Scene &scene, Project::Object &obj, bool keyDelete,
    bool parentEnabled = true
  )
  {
    ImGuiTreeNodeFlags flag = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow
      | ImGuiTreeNodeFlags_OpenOnDoubleClick
      | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAllColumns;

    if (obj.children.empty()) {
      flag |= ImGuiTreeNodeFlags_Leaf;
    }

    bool isSelected = ctx.isObjectSelected(obj.uuid);
    if (isSelected) {
      flag |= ImGuiTreeNodeFlags_Selected;
    }

    if (isSelected && obj.parent && keyDelete) {
      deleteSelection = true;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 3_px));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));

    std::string nameID = getNodeIcons(obj) + obj.name;
    if (obj.parent) nameID += " (" + std::to_string(obj.runtimeId) + ")";
    nameID += "##" + std::to_string(obj.uuid);

    // Dim prefab-managed subobjects that are outside the active edit scope (cannot be edited
    // until the owning prefab is put in edit mode), so it's clear where edits will land.
    const bool dimmed = scene.isPrefabSubobject(obj)
      && !scene.resolveEditTarget(obj).parent
      && !scene.resolveAddTarget(obj).parent;
    if (dimmed) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));

    bool isOpen = ImGui::TreeNodeEx(nameID.c_str(), flag);

    if (dimmed) ImGui::PopStyleColor();

    // Tooltip: which prefab owns this node (and whether it's locked in the current scope).
    if (obj.uuidPrefab.value && ImGui::IsItemHovered()) {
      auto *passet = ctx.project->getAssets().getEntryByUUID(obj.uuidPrefab.value);
      ImGui::SetTooltip("Prefab: %s%s",
        passet ? passet->name.c_str() : "?",
        dimmed ? "  (locked - edit its prefab to change)" : "");
    }

    ImGui::PopStyleVar(2);
    ImVec2 nodeRectMin = ImGui::GetItemRectMin();

    bool nodeIsClicked = ImGui::IsItemHovered()
      && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
      && !ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    bool nodeIsDoubleClicked = ImGui::IsItemHovered()
      && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
      && !ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
      ImGui::OpenPopup("NodePopup");
    }

    // Double-clicked a node --> Start renaming
    if (nodeIsDoubleClicked)
      startRenaming(obj.uuid);

    bool isRenaming = renameObjectUUID == obj.uuid;

    if (obj.parent && ImGui::BeginDragDropSource())
    {
      ImGui::SetDragDropPayload("OBJECT", &obj.uuid, sizeof(obj.uuid));
      ImGui::TextUnformatted(obj.name.c_str());
      ImGui::EndDragDropSource();
    }

    if (obj.parent && ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OBJECT")) {
        dragDropTask.sourceUUID = *((uint32_t*)payload->Data);
        dragDropTask.targetUUID = obj.uuid;
        dragDropTask.isInsert = true;
      }
      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET")) {
        prefabDropTask.prefabUUID = *((uint64_t*)payload->Data);
        prefabDropTask.targetNodeUUID = obj.uuid;
      }
      ImGui::EndDragDropTarget();
    }

    if (!obj.parent && ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET")) {
        prefabDropTask.prefabUUID = *((uint64_t*)payload->Data);
        prefabDropTask.targetNodeUUID = obj.uuid;
      }
      ImGui::EndDragDropTarget();
    }

    // Is renaming the object node
    if (isRenaming)
      drawRenameInput(obj, nodeRectMin);

    if(obj.parent)
    {
      float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
      ImVec2 iconSize{16_px, 21_px};

      auto oldCursorPos = ImGui::GetCursorPos();

      float offsetRight = calcRightControlAreaWidth();
      ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - offsetRight);

      if(!parentEnabled)ImGui::BeginDisabled();

      ImGui::PushID(("vis_" + std::to_string(obj.uuid)).c_str());

      int clicked = 0;
      clicked |= ImGui::IconToggle(obj.selectable, ICON_MDI_CURSOR_DEFAULT, ICON_MDI_CURSOR_DEFAULT_OUTLINE, iconSize);
      ImGui::SetItemTooltip("%s Object Selection", obj.selectable ? "Disable" : "Enable");
      ImGui::SameLine(0, spacing);
      clicked |= ImGui::IconToggle(obj.enabled, ICON_MDI_CHECKBOX_MARKED, ICON_MDI_CHECKBOX_BLANK_OUTLINE, iconSize);
      ImGui::SetItemTooltip("%s Object", obj.enabled ? "Disable" : "Enable");

      if(clicked)nodeIsClicked = false;

      ImGui::PopID();

      if(!parentEnabled)ImGui::EndDisabled();
      ImGui::SetCursorPosY(oldCursorPos.y);
    }

    if(ImGui::IsDragDropActive()) {
      if(DrawDropTarget(dragDropTask.sourceUUID, obj.uuid)) {
        dragDropTask.targetUUID = obj.uuid;
      }
    }

    if (nodeIsClicked) {
      bool isCtrlDown = ImGui::GetIO().KeyCtrl;
      if (isCtrlDown) {
        ctx.toggleObjectSelection(obj.uuid);
      } else {
        ctx.setObjectSelection(obj.uuid);
      }
      //ImGui::SetWindowFocus("Object");
      //ImGui::SetWindowFocus("Graph");
    }

    if(isOpen)
    {
      if (ImGui::BeginPopupContextItem("NodePopup"))
      {
        // Structural edits inside a prefab instance only apply while editing the prefab.
        const bool isPrefab = obj.isPrefabInstance();
        const bool isSubobject = scene.isPrefabSubobject(obj);
        const bool editingPrefab = isPrefab && scene.isEditingPrefabOf(obj);

        const bool addDisabled = isPrefab && !editingPrefab;
        if (addDisabled) ImGui::BeginDisabled();
        if (ImGui::MenuItem(ICON_MDI_CUBE_OUTLINE " Add Object")) {
          if (isPrefab) {
            // Defer: prefabAddChild reconciles (mutates the tree we're iterating).
            prefabEditTask = {PrefabEditTask::Type::ADD_CHILD, obj.uuid};
          } else {
            auto added = scene.addObject(obj);
            if (added) {
              ctx.setObjectSelection(added->uuid);
              startRenaming(added->uuid);
            }
            Editor::UndoRedo::getHistory().markChanged("Add Object");
          }
        }
        if (addDisabled) ImGui::EndDisabled();

        if (obj.parent) {
          if (!obj.isPrefabInstance() && ImGui::MenuItem(ICON_MDI_PACKAGE_VARIANT_CLOSED_PLUS " To Prefab")) {
            scene.createPrefabFromObject(obj.uuid);
          }

          // Subobjects can only be deleted while editing the prefab; instance roots
          // (and plain objects) delete normally as scene objects.
          const bool delDisabled = isSubobject && !editingPrefab;
          if (delDisabled) ImGui::BeginDisabled();
          if (ImGui::MenuItem(ICON_MDI_TRASH_CAN " Delete")) {
            if (isSubobject) {
              prefabEditTask = {PrefabEditTask::Type::REMOVE_NODE, obj.uuid};
            } else {
              deleteObj = &obj;
            }
          }
          if (delDisabled) ImGui::EndDisabled();
        }
        ImGui::EndPopup();
      }

      for(auto &child : obj.children) {
        drawObjectNode(scene, *child, keyDelete, parentEnabled && obj.enabled);
      }

      ImGui::TreePop();
    }
  }
}

void Editor::SceneGraph::draw()
{
  auto scene = ctx.project->getScenes().getLoadedScene();
  if (!scene)return;

  // Breadcrumb: which prefab(s) are currently in edit mode (the active edit scope). Makes it
  // explicit which prefab a drop/edit will modify.
  {
    std::vector<std::string> editScopes;
    std::vector<Project::Object*> stack{ &scene->getRootObject() };
    while (!stack.empty()) {
      Project::Object *o = stack.back();
      stack.pop_back();
      if (o->isPrefabEdit && scene->isPrefabInstanceRoot(*o)) {
        uint64_t pf = ctx.project->getAssets().resolveInstance(*o).prefabUUID;
        auto *a = ctx.project->getAssets().getEntryByUUID(pf);
        editScopes.push_back(a ? a->name : std::string{"?"});
      }
      for (auto &c : o->children) stack.push_back(c.get());
    }
    if (!editScopes.empty()) {
      std::string bc = ICON_MDI_PENCIL " Editing prefab: ";
      for (size_t i = 0; i < editScopes.size(); ++i) {
        bc += editScopes[i];
        if (i + 1 < editScopes.size()) bc += "  >  ";
      }
      ImGui::TextDisabled("%s", bc.c_str());
      ImGui::Separator();
    }
  }

  dragDropTask = {};
  prefabDropTask = {};
  prefabEditTask = {};
  deleteObj = nullptr;
  deleteSelection = false;
  bool isFocus = ImGui::IsWindowFocused();
  // While rename is active, shortcuts stay disabled, so the text field can own the keyboard input
  bool isRenaming = renameObjectUUID != 0;

  ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 16.0_px);
  bool keyDelete = isFocus && !isRenaming && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace));
  // F2 starts renaming the current object, matching common scene-tree/file-explorer behavior
  bool keyRename = isFocus && !isRenaming && ImGui::IsKeyPressed(ImGuiKey_F2);

  if (keyRename) {
    const std::vector<uint32_t> &selectedIds = ctx.getSelectedObjectUUIDs();
    // Inline renaming only makes sense for a single target; multi-select keeps its current state
    if (selectedIds.size() == 1) {
      // Rename the selected object
      startRenaming(selectedIds.front());
    }
  }

  auto &root = scene->getRootObject();
  drawObjectNode(*scene, root, keyDelete);

  ImGui::PopStyleVar(1);

  bool isCtrlDown = ImGui::GetIO().KeyCtrl;
  if (!isCtrlDown
      && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
      && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
      && !ImGui::IsAnyItemHovered()) {
    ctx.clearObjectSelection();
  }

  if(dragDropTask.sourceUUID && dragDropTask.targetUUID) {
    //printf("dragDropTarget %08X -> %08X (%d)\n", dragDropTask.sourceUUID, dragDropTask.targetUUID, dragDropTask.isInsert);
    auto src = scene->getObjectByUUID(dragDropTask.sourceUUID);
    auto tgt = scene->getObjectByUUID(dragDropTask.targetUUID);

    // Prefab-aware handling first (reparent within / drag in / drag out of a prefab being edited).
    int prefabResult = scene->prefabMove(src.get(), tgt.get(), dragDropTask.isInsert);
    if (prefabResult == 1) {
      UndoRedo::getHistory().markChanged("Move Object");
    } else if (prefabResult == 0) {
      // Not a prefab move --> normal scene reparent
      bool moved = scene->moveObject(
        dragDropTask.sourceUUID,
        dragDropTask.targetUUID,
        dragDropTask.isInsert
      );
      if (moved)
        UndoRedo::getHistory().markChanged("Move Object");
    }
    // prefabResult == 2: intentionally ignored (locked subobject) --> no-op
  }

  if (prefabEditTask.type != PrefabEditTask::Type::NONE) {
    auto node = scene->getObjectByUUID(prefabEditTask.nodeUUID);
    if (node) {
      if (prefabEditTask.type == PrefabEditTask::Type::ADD_CHILD) {
        auto added = scene->prefabAddChild(*node);
        if (added) {
          ctx.setObjectSelection(added->uuid);
          startRenaming(added->uuid);
        }
        UndoRedo::getHistory().markChanged("Add Prefab Subobject");
      } else if (prefabEditTask.type == PrefabEditTask::Type::REMOVE_NODE) {
        scene->prefabRemoveNode(*node);
        UndoRedo::getHistory().markChanged("Remove Prefab Subobject");
      }
    }
  }

  if (prefabDropTask.prefabUUID && prefabDropTask.targetNodeUUID) {
    auto targetObj = scene->getObjectByUUID(prefabDropTask.targetNodeUUID);
    Project::Object* parent = targetObj ? targetObj.get() : &scene->getRootObject();

    std::shared_ptr<Project::Object> newObj;
    if (parent->isPrefabInstance()) {
      // Dropping a prefab onto a prefab instance embeds it INTO the prefab being edited
      // (routed to the innermost edited prefab, or an additive override on an outer one).
      // If nothing in that instance is being edited, the drop is locked (ignored).
      if (scene->isEditingPrefabOf(*parent)) {
        UndoRedo::getHistory().markChanged("Add Prefab to Prefab");
        newObj = scene->addPrefabReferenceToPrefab(*parent, prefabDropTask.prefabUUID);
      }
    } else {
      UndoRedo::getHistory().markChanged("Add Prefab");
      newObj = scene->addPrefabInstance(prefabDropTask.prefabUUID, *parent);
    }
    if (newObj) ctx.setObjectSelection(newObj->uuid);
  }

  if (deleteSelection || deleteObj) {
    if (deleteObj && !ctx.isObjectSelected(deleteObj->uuid)) {
      ctx.setObjectSelection(deleteObj->uuid);
    }

    UndoRedo::getHistory().markChanged("Delete Object");
    Editor::SelectionUtils::deleteSelectedObjects(*scene);
  }
}
