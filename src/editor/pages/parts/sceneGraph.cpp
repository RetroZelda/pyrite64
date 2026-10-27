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
#include "../../imgui/notification.h"
#include "IconsMaterialDesignIcons.h"
#include "imgui_internal.h"
#include "../../undoRedo.h"
#include "../../selectionUtils.h"
#include "../../splineEdit.h"
#include "../../../project/component/types/compSpline.h"

namespace
{
  Project::Object* deleteObj{nullptr};
  bool deleteSelection{false};
  uint32_t renameObjectUUID{0};
  std::string renameBuffer{};
  bool startingRename{false};
  glm::vec3 rootViewCenter{0.0f}; // SceneGraph::draw()'s viewCenter

  // Set per-frame at the start of draw(). When non-null a prefab is being edited and
  // selection is restricted to its own definition, with everything else dimmed and inert.
  Project::Object* prefabEditObj{nullptr};

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

    // Is a prefab --> Add prefab icon
    if(obj.uuidPrefab.value)
      prefix += ICON_MDI_PACKAGE_VARIANT_CLOSED " ";

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

  /**
   * Turns a freshly added child of `parent` into a "Spline Path": its name, a Spline component (the
   * default loop) and a position at the parent (editor positions are world space) or, under the
   * scene root, at the centre of the active 3D viewport, so the path is in view right away.
   *
   * @param added  The new scene object.
   * @param parent The object it was added under.
   */
  void initSplinePath(Project::Object &added, Project::Object &parent)
  {
    const glm::vec3 pos = parent.parent ? parent.pos.resolve(parent.propOverrides) : rootViewCenter;
    added.name = "Spline Path";
    added.pos.value = pos;
    added.addComponent(Project::Component::Spline::ID);
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
        obj.name = renameBuffer;
        Editor::UndoRedo::getHistory().markChanged("Edit object name");
      }
      clearRenaming();
    }

    ImGui::SetCursorPos(oldCursorPos);
  }

  // Display of a prefab instance's definition tree (nested prefab content). The nodes
  // aren't scene objects, so they're shown dimmed and selecting one targets it as a
  // nested override (rootUuid = instance, path = chain of definition-node uuids).
  void drawPrefabDefNode(Project::Object &node, int depth, uint32_t rootUuid,
                         std::vector<uint32_t> path, bool selectable)
  {
    if(depth > 64)return; // guard against self-referencing prefabs
    path.push_back(node.uuid);

    Project::Object* src = Editor::SelectionUtils::prefabDefOf(&node);

    bool isSelected = (ctx.selObjectUUID == rootUuid && ctx.selSubPath == path);
    bool dim = prefabEditObj ? !selectable : false;

    ImGuiTreeNodeFlags flag = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow
      | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_FramePadding
      | ImGuiTreeNodeFlags_SpanAllColumns;
    if(src->children.empty())flag |= ImGuiTreeNodeFlags_Leaf;
    if(isSelected)flag |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 3_px));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
    std::string nameID = getNodeIcons(node) + node.name + "##pf"
      + std::to_string(reinterpret_cast<uintptr_t>(&node));
    if(dim)ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    bool isOpen = ImGui::TreeNodeEx(nameID.c_str(), flag);
    if(dim)ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    if(selectable && ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
      ctx.setNestedSelection(rootUuid, path);
    }

    if(isOpen) {
      // Outside edit mode the whole def tree is selectable.
      // In edit mode we may descend through regular children but stop at nested prefab instances.
      bool childSelectable = prefabEditObj ? (selectable && !node.isPrefabInstance()) : true;
      for(auto &child : src->children) {
        drawPrefabDefNode(*child, depth + 1, rootUuid, path, childSelectable);
      }
      ImGui::TreePop();
    }
  }

  void drawObjectNode(
    Project::Scene &scene, Project::Object &obj, bool keyDelete,
    bool parentEnabled = true
  )
  {
    ImGuiTreeNodeFlags flag = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow
      | ImGuiTreeNodeFlags_OpenOnDoubleClick
      | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAllColumns;

    // A prefab instance shows its definition tree (read-only) below, so it can expand
    // even though the thin instance itself has no children.
    Project::Object* prefabDef = nullptr;
    if(obj.isPrefabInstance()) {
      auto prefab = ctx.project->getAssets().getPrefabByUUID(obj.uuidPrefab.value);
      if(prefab && !prefab->obj.children.empty())prefabDef = &prefab->obj;
    }

    if (obj.children.empty() && !prefabDef) {
      flag |= ImGuiTreeNodeFlags_Leaf;
    }

    bool isSelected = ctx.isObjectSelected(obj.uuid);
    if (isSelected) {
      flag |= ImGuiTreeNodeFlags_Selected;
    }

    if (isSelected && obj.parent && keyDelete) {
      deleteSelection = true;
    }

    // While editing a prefab, only that instance may be selected here. Its own definition
    // is handled by drawPrefabDefNode. All other scene objects are dimmed and inert.
    bool canSelect = !prefabEditObj || (&obj == prefabEditObj);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 3_px));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));

    std::string nameID = getNodeIcons(obj) + obj.name;
    if (obj.parent) nameID += " (" + std::to_string(obj.runtimeId) + ")";
    nameID += "##" + std::to_string(obj.uuid);

    if(!canSelect)ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    bool isOpen = ImGui::TreeNodeEx(nameID.c_str(), flag);
    if(!canSelect)ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImVec2 nodeRectMin = ImGui::GetItemRectMin();

    // Mark object being edited in prefab-edit mode
    if(ctx.isPrefabEditing(obj.uuid)) {
      ImVec2 bgMax = ImGui::GetItemRectMax();
      bgMax.x = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
      ImU32 editCol = ImGui::Theme::getColorU32("prefabEditBg", IM_COL32(190, 55, 55, 60));
      ImGui::GetWindowDrawList()->AddRectFilled(nodeRectMin, bgMax, editCol);
    }

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

    if (nodeIsClicked && canSelect) {
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
        if (ImGui::MenuItem(ICON_MDI_CUBE_OUTLINE " Add Object")) {
          auto added = scene.addObject(obj);
          if (added) {
            ctx.setObjectSelection(added->uuid);
            startRenaming(added->uuid);
          }
          Editor::UndoRedo::getHistory().markChanged("Add Object");
        }

        // New object with a Spline component (AI patrol path etc.), ready for point editing.
        if (ImGui::MenuItem(ICON_MDI_VECTOR_CURVE " Add Spline Path")) {
          auto added = scene.addObject(obj);
          if (added) {
            initSplinePath(*added, obj);
            ctx.setObjectSelection(added->uuid);
            startRenaming(added->uuid);
          }
          Editor::UndoRedo::getHistory().markChanged("Add Spline Path");
        }

        if (obj.parent) {
          if (!obj.isPrefabInstance() && ImGui::MenuItem(ICON_MDI_PACKAGE_VARIANT_CLOSED_PLUS " To Prefab")) {
            // Defer: createPrefabFromObject reloads assets (frees GPU textures), which is
            // unsafe mid-frame while ImGui draw data still references them.
            auto *scenePtr = &scene;
            uint32_t uuid = obj.uuid;
            ctx.deferAction([scenePtr, uuid]() { scenePtr->createPrefabFromObject(uuid); });
          }

          if (obj.isPrefabInstance() && ImGui::MenuItem(ICON_MDI_PACKAGE_VARIANT " Unpack Prefab")) {
            // Defer: modifies the scene tree (adds objects) - unsafe mid-iteration.
            auto *scenePtr = &scene;
            uint32_t uuid = obj.uuid;
            ctx.deferAction([scenePtr, uuid]() {
              Editor::UndoRedo::getHistory().markChanged("Unpack Prefab");
              scenePtr->unpackPrefabInstance(uuid);
            });
          }

          if (ImGui::MenuItem(ICON_MDI_TRASH_CAN " Delete"))deleteObj = &obj;
        }
        ImGui::EndPopup();
      }

      for(auto &child : obj.children) {
        drawObjectNode(scene, *child, keyDelete, parentEnabled && obj.enabled);
      }

      // Prefab definition tree showing nested prefab content under the instance. Nodes are
      // selectable for nested override editing, keyed relative to the prefab root. While
      // editing a prefab, only the edited instance's own definition is selectable.
      if(prefabDef) {
        for(auto &child : prefabDef->children) {
          drawPrefabDefNode(*child, 0, obj.uuid, {}, canSelect);
        }
      }

      ImGui::TreePop();
    }
  }
}

void Editor::SceneGraph::draw(const glm::vec3 &viewCenter)
{
  auto scene = ctx.project->getScenes().getLoadedScene();
  if (!scene)return;
  rootViewCenter = viewCenter;

  dragDropTask = {};
  prefabDropTask = {};
  deleteObj = nullptr;
  deleteSelection = false;
  prefabEditObj = Editor::SelectionUtils::getPrefabEditObject(*scene);
  bool isFocus = ImGui::IsWindowFocused();
  // While rename is active, shortcuts stay disabled, so the text field can own the keyboard input
  bool isRenaming = renameObjectUUID != 0;

  ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 16.0_px);
  // While editing spline points, Delete removes points (in the viewport) and never the edited object,
  // which stays selected; the context menu's Delete still removes it (and ends point edit).
  bool keyDelete = isFocus && !isRenaming && !Editor::SplineEdit::isEditing()
    && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace));
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
    bool moved = scene->moveObject(
      dragDropTask.sourceUUID,
      dragDropTask.targetUUID,
      dragDropTask.isInsert
    );

    // Could move --> Add to history
    if (moved)
      UndoRedo::getHistory().markChanged("Move Object");
  }

  if (prefabDropTask.prefabUUID && prefabDropTask.targetNodeUUID) {
    auto targetObj = scene->getObjectByUUID(prefabDropTask.targetNodeUUID);
    Project::Object* parent = targetObj ? targetObj.get() : &scene->getRootObject();

    std::shared_ptr<Project::Object> newObj;
    if (parent->isPrefabInstance() && ctx.isPrefabEditing(parent->uuid)) {
      // Nesting a prefab into the definition being edited is not supported yet; outside of
      // edit mode the drop adds a regular scene child under the instance.
      Editor::Noti::add(Editor::Noti::INFO, "Drop the prefab outside of prefab-edit mode to place it in the scene");
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
