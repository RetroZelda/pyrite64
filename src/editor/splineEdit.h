/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>
#include <vector>
#include "imgui.h"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/mat4x4.hpp"

namespace Project { class Scene; }

/**
 * Spline point editor.
 *
 * ONE global session (object uuid + Spline component uuid + selected point indices) shown by every
 * viewport and the inspector. Only uuids/indices are stored: undo/redo and reloads recreate every
 * Object/Entry/Data, so the target is re-resolved on every call. Viewports keep transient input state
 * only (ViewportState). Every mutation records exactly ONE undo step (drags: on release); an undo/redo
 * while a handle press, handle drag or point gizmo drag runs cancels it without recording anything.
 */
namespace Editor::SplineEdit
{
  // Per-viewport transient input state (member of Viewport3D; never shared between viewports).
  struct ViewportState
  {
    bool gizmoActive{false};      // the point gizmo moved points during the current drag
    bool gizmoGrabbed{false};     // the point gizmo is grabbed (dragGen recorded at the grab, before any move)
    bool pressPending{false};     // LMB went down on empty image space (click or box)
    bool boxDragging{false};      // ... and moved > 4 px
    bool handleArmed{false};      // LMB went down on a handle
    bool handleDragging{false};   // ... and moved > 3 px: selected points follow the mouse
    bool ignoreUntilRelease{false}; // Esc or an undo/redo cancelled the press
    int pressHandle{-1};
    glm::vec2 pressPos{0.0f};     // image-local px
    glm::vec2 curPos{0.0f};
    glm::vec3 planePoint{0.0f};   // world drag plane
    glm::vec3 planeNormal{0.0f, 1.0f, 0.0f};
    glm::vec3 grabOffset{0.0f};
    std::vector<int> dragIdx{};           // point indices moved by the handle drag
    std::vector<glm::vec3> dragStart{};   // their world positions at press
    int dragGrab{-1};                     // index into dragIdx of the grabbed handle
    uint64_t dragGen{0};                  // UndoRedo generation at the handle press / first gizmo move (a newer one cancels)
    glm::vec2 ghostMouse{-1.0f};  // Shift ghost raycast cache (recomputed when the mouse moves)
    bool ghostHit{false};
    glm::vec3 ghostPos{0.0f};
    glm::mat4 ghostViewProj{0.0f}; // ... or the camera moves (proj * view the cache was made with)
  };

  // What Viewport3D::draw() knows about itself this frame.
  struct ViewInfo
  {
    const glm::mat4 *view{nullptr};   // uniGlobal.cameraMat (previous render pass, same as gizmo/box select)
    const glm::mat4 *proj{nullptr};   // uniGlobal.projMat
    ImVec2 imageMin{0.0f, 0.0f};      // screen position of the viewport image
    ImVec2 imageSize{0.0f, 0.0f};     // displayed image size (currSize)
    bool hovered{false};              // mouse over the image
    bool mouseBlocked{false};         // Alt-orbit / view cube / camera fly own the mouse
    bool worldSpace{true};            // gizmo axes world (true) or object-local
    bool overlaysHidden{false};       // camera-resolution clean preview
    bool showSplines{true};           // this viewport's "show splines" toggle
    bool objectGizmoHovered{false};   // the object gizmo is under the mouse / in use (it wins over passive handles)
  };

  // --- session -------------------------------------------------------------------------------
  [[nodiscard]] bool isEditing();                                     // any session active
  [[nodiscard]] bool isEditing(uint32_t objUUID, uint64_t compUUID);
  bool begin(uint32_t objUUID, uint64_t compUUID, int selectPoint = -1); // false (+ notification) if missing / locked
  void end();
  void validate(Project::Scene *scene);   // once per frame at the top of Editor::Scene::draw(); no-op during builds
  bool toggleForSelection();              // Tab/P: end, or begin on the single selected object's first Spline

  // --- point selection (indices into the edited component's point list) ----------------------
  [[nodiscard]] const std::vector<int>& selection();   // sorted, unique; empty without a session
  [[nodiscard]] bool isPointSelected(int idx);
  void selectPoint(int idx, bool toggle);              // also becomes the anchor
  void selectAll();                                    // all selected -> clears instead
  void clearSelection();
  void setInspectorHover(int idx);                     // inspector row hover (-1 none), valid for 2 frames

  // --- queries -------------------------------------------------------------------------------
  [[nodiscard]] bool canEditPoints(uint32_t objUUID, uint64_t compUUID);
  bool getFocusBounds(glm::vec3 &center, float &radius);   // selected points (or all) of the session, world

  // --- operations: each records ONE undo step (markChanged + markPrefabDirty for template data) ---
  bool extrude(uint32_t objUUID, uint64_t compUUID);                                  // from the anchor / end
  bool insertAfter(uint32_t objUUID, uint64_t compUUID, std::vector<int> after);      // empty = append
  bool removePoints(uint32_t objUUID, uint64_t compUUID, std::vector<int> indices);
  bool reversePoints(uint32_t objUUID, uint64_t compUUID);
  bool setWait(uint32_t objUUID, uint64_t compUUID, const std::vector<int> &indices, uint8_t waitDs);
  // Each point straight down onto the closest surface below it that shows its drawn side (never up onto an
  // overhang); culled sides are seen through, as in the viewport (e.g. a one-sided ceiling from above). A point
  // inside a one-sided solid (culled sides below AND above) goes up onto that surface, and a point under the
  // level onto the closest surface whose top is drawn, at most 100 units. Empty = all; returns moved count.
  int  dropToFloor(uint32_t objUUID, uint64_t compUUID, std::vector<int> indices);
  void commit(uint32_t objUUID, uint64_t compUUID, const char *reason);               // after a direct Data edit

  // --- viewport hooks (called by Viewport3D::draw) -------------------------------------------
  // Before the viewport starts an object pick/box select: true = the spline editor owns this click. Object
  // mode: only a press on a passive handle not covered by the object gizmo (objectGizmoHovered).
  // A clean preview (overlaysHidden) shows no point editor and claims nothing: a click there is an object
  // click even during point edit (selecting anything else ends it, see validate).
  [[nodiscard]] bool wantsMouse(const ViewInfo &vi, glm::vec2 mouseLocal);
  // Inside ImGuizmo::PushID(winId) after ImGuizmo::SetRect: overlay, point gizmo, mouse and keys.
  // In a clean preview only Esc and the edit toggle key act, and only to leave point edit (no overlay, no
  // point keys, the toggle never starts a session there).
  void drawViewport(const ViewInfo &vi, ViewportState &vs);
  // Object mode: spline object whose curve passes within radiusPx of mouseLocal (0 = none). Only where its
  // depth-tested preview line is not hidden behind static geometry.
  [[nodiscard]] uint32_t pickCurve(const ViewInfo &vi, glm::vec2 mouseLocal, float radiusPx);
  // true if the object (or its prefab source node) has a Model (Static) component
  [[nodiscard]] bool isStaticGeometry(Project::Scene &scene, uint32_t objUUID);
}
