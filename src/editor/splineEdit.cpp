/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "splineEdit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <string>
#include "ImGuizmo.h"
#include "IconsMaterialDesignIcons.h"
#include "glm/common.hpp"
#include "glm/geometric.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "../context.h"
#include "undoRedo.h"
#include "sceneRaycast.h"
#include "imgui/notification.h"
#include "imgui/theme.h"
#include "../project/assetManager.h"
#include "../project/scene/scene.h"
#include "../project/component/types/compSpline.h"
#include "engine/include/lib/splineMath.h"

namespace
{
  namespace SM = P64::SplineMath;
  namespace Spline = Project::Component::Spline;
  using Editor::SplineEdit::ViewInfo;
  using Editor::SplineEdit::ViewportState;

  constexpr float HANDLE_RADIUS  = 5.0f;      // px, times the UI zoom
  constexpr float PICK_RADIUS    = 8.0f;      // px, times the UI zoom: handles and Shift+Click on the curve
  constexpr float HANDLE_DRAG_PX = 3.0f;      // a handle press turns into a drag after this many px
  constexpr float BOX_DRAG_PX    = 4.0f;      // an empty-space press turns into a box (as the viewport's)
  constexpr float DEFAULT_STEP   = 100.0f;    // local units for points added without a direction
  constexpr float SNAP_GRID      = 10.0f;     // Ctrl drag and Shift+S, same grid as the object gizmo
  constexpr float STEEP_RAY_Y    = 0.2f;      // flatter mouse rays drag on a camera-facing plane instead
  constexpr float DROP_LIFT      = 100.0f;    // Drop to Floor lifts a point inside geometry by at most this much
  constexpr float DROP_ON_EPS    = 1.0f;      // ... and a point this close above (or inside) a surface lies on it
  constexpr float PICK_TIE_PX    = 0.5f;      // curve picks closer than this count as a tie (nearest depth wins)

  constexpr ImU32 COL_PASSIVE    = IM_COL32(180, 180, 180, 255);
  constexpr ImU32 COL_LOCKED     = IM_COL32(110, 110, 110, 255);
  constexpr ImU32 COL_START      = IM_COL32( 80, 220,  80, 255);
  constexpr ImU32 COL_FILL       = IM_COL32(230, 230, 230, 255);
  constexpr ImU32 COL_SELECTED   = IM_COL32(255, 150,  40, 255);
  constexpr ImU32 COL_OUTLINE    = IM_COL32( 30,  30,  30, 255);
  constexpr ImU32 COL_ROW_HOVER  = IM_COL32(255, 225,  40, 255);
  constexpr ImU32 COL_LABEL      = IM_COL32(255, 255, 255, 230);
  constexpr ImU32 COL_SHADOW     = IM_COL32(  0,   0,   0, 200);
  constexpr ImU32 COL_GHOST      = IM_COL32(255, 255, 255, 128);
  constexpr ImU32 COL_GHOST_ADD  = IM_COL32( 80, 230,  80, 255);
  constexpr ImU32 COL_BANNER_BG  = IM_COL32(  0,   0,   0, 160);
  constexpr ImU32 COL_BANNER_TXT = IM_COL32(255, 255, 255, 255);
  constexpr ImU32 COL_BANNER_DIM = IM_COL32(200, 200, 200, 255);
  constexpr ImU32 COL_BANNER_WARN= IM_COL32(255, 200,  80, 255);

  struct Session
  {
    bool active{false};
    uint32_t objUUID{0};
    uint64_t compUUID{0};
    std::vector<int> sel{};   // sorted, unique point indices
    int anchor{-1};           // active point: extrude / Shift+Click continue from here (-1 = none)
    int hoverRow{-1};         // inspector point row under the mouse
    int hoverRowFrame{-100};
  };

  Session session{};
  int dragDepth{0};          // viewports dragging points right now (gizmo or handle)
  int dragSeenFrame{-100};   // last frame a viewport reported such a drag
  ImDrawListSplitter overlaySplitter{};

  // Valid for the current call only: undo/redo and reloads recreate every Object/Entry/Data.
  struct Target
  {
    Project::Scene *scene{nullptr};
    Project::Object *obj{nullptr};            // scene object (the instance for prefabs)
    Project::Component::Entry *entry{nullptr};
    Spline::Data *data{nullptr};
    bool templateOwned{false};                // data lives in a prefab template, shared by all instances
    bool editable{false};
    Spline::WorldTransform xf{};
  };

  struct ScreenPt
  {
    glm::vec2 pos{};    // image-local px
    float depth{0.0f};  // 0 = near .. 1 = far
    bool visible{false};
  };

  struct PieceHit
  {
    bool hit{false};
    uint32_t piece{0};  // polyline piece: sample -> sample + 1
    float t{0.0f};      // fraction along the piece (screen space)
    float dist{0.0f};   // px
    float depth{0.0f};
    glm::vec2 pos{};    // image-local px
  };

  // Where a Shift+Click point goes: the index it is inserted at and the point it attaches to.
  struct Placement
  {
    int insertAt{0};
    int ref{-1};        // -1: no points yet
  };

  Project::Scene* loadedScene()
  {
    return ctx.project ? ctx.project->getScenes().getLoadedScene() : nullptr;
  }

  bool resolveIn(Project::Scene *scene, uint32_t objUUID, uint64_t compUUID, Target &t)
  {
    t = Target{};
    if(!scene || !ctx.project || objUUID == 0)return false;
    auto obj = scene->getObjectByUUID(objUUID);
    if(!obj)return false;

    auto &assets = ctx.project->getAssets();
    auto found = Spline::findEntry(*obj, compUUID, assets);
    if(!found.entry || !found.entry->data)return false;

    t.scene = scene;
    t.obj = obj.get();
    t.entry = found.entry;
    t.data = &Spline::getData(*found.entry);
    t.templateOwned = found.templateOwned;
    // a spline of the prefab definition is editable while that instance is in prefab-edit mode
    // (ImTable::isPrefabLocked would also block instance-additive splines)
    t.editable = !found.templateOwned || ctx.isPrefabEditing(obj->uuid);
    t.xf = Spline::getWorldTransform(*obj, assets);
    return true;
  }

  bool resolve(uint32_t objUUID, uint64_t compUUID, Target &t)
  {
    return resolveIn(loadedScene(), objUUID, compUUID, t);
  }

  // The single selected object's first Spline (it may be locked).
  bool resolveSelectedSpline(Target &t)
  {
    t = Target{};
    auto *scene = loadedScene();
    if(!scene || ctx.selObjectUUIDs.size() != 1)return false;
    auto obj = scene->getObjectByUUID(ctx.selObjectUUIDs.front());
    if(!obj)return false;
    auto found = Spline::findFirstSpline(*obj, ctx.project->getAssets());
    if(!found.entry)return false;
    return resolveIn(scene, obj->uuid, found.entry->uuid, t);
  }

  // The ONLY place point edits are recorded (one undo step per call).
  void commitTarget(const Target &t, const char *reason)
  {
    Editor::UndoRedo::getHistory().markChanged(reason);
    if(!t.templateOwned || !ctx.project)return;

    // template data is not part of the scene snapshot (not undoable): flag the prefab for saving
    ctx.project->getAssets().markPrefabDirty(t.obj->uuidPrefab.value);
  }

  bool isSessionTarget(uint32_t objUUID, uint64_t compUUID)
  {
    return session.active && session.objUUID == objUUID && session.compUUID == compUUID;
  }

  void normalizeIndices(std::vector<int> &idx)
  {
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
  }

  bool indicesValid(const std::vector<int> &idx, size_t count)
  {
    return std::all_of(idx.begin(), idx.end(), [count](int i) { return i >= 0 && (size_t)i < count; });
  }

  // Drops selection entries an undo/redo (or a smaller point list) left out of range.
  void clampSession(size_t count)
  {
    std::erase_if(session.sel, [count](int i) { return i < 0 || (size_t)i >= count; });
    normalizeIndices(session.sel);
    if(session.anchor < 0 || (size_t)session.anchor >= count)session.anchor = -1;
  }

  void selectOnly(uint32_t objUUID, uint64_t compUUID, int idx)
  {
    if(!isSessionTarget(objUUID, compUUID))return;
    session.sel.clear();
    if(idx >= 0)session.sel.push_back(idx);
    session.anchor = idx;
  }

  bool hasRoom(size_t count)
  {
    if(count < Spline::MAX_POINTS)return true;
    Editor::Noti::add(Editor::Noti::ERROR, "A spline can have at most " + std::to_string(Spline::MAX_POINTS) + " points");
    return false;
  }

  // Operations are no-ops while any viewport drags points. Self-healing: a viewport closed or hidden
  // mid-drag never reports its release, so a count nobody refreshed for a few frames is dropped.
  bool dragBlocked()
  {
    if(dragDepth > 0 && ImGui::GetFrameCount() - dragSeenFrame > 2)dragDepth = 0;
    return dragDepth > 0;
  }

  // Keeps dragDepth balanced with ViewportState::handleDragging / gizmoActive.
  void setDragging(bool &flag, bool on)
  {
    if(flag == on)return;
    flag = on;
    if(on) {
      ++dragDepth;
      dragSeenFrame = ImGui::GetFrameCount();
    } else if(dragDepth > 0) {
      --dragDepth;
    }
  }

  // Target for a point operation: exists, editable and no point drag running.
  bool resolveForEdit(uint32_t objUUID, uint64_t compUUID, Target &t)
  {
    if(dragBlocked())return false;
    return resolve(objUUID, compUUID, t) && t.editable;
  }

  glm::vec3 toGlm(const SM::Vec3 &v) { return {v.x, v.y, v.z}; }
  glm::vec3 toGlm(const SM::Sample &s) { return {s.x, s.y, s.z}; }

  float snapGrid(float v) { return std::round(v / SNAP_GRID) * SNAP_GRID; }

  glm::vec3 extrudeFrom(const glm::vec3 &end, const glm::vec3 &prev)
  {
    glm::vec3 step = end - prev;
    if(glm::length(step) < 1.0f)step = {DEFAULT_STEP, 0.0f, 0.0f};
    return end + step;
  }

  // Local position of a point inserted after index `a` (-1 = before the first one), computed from the
  // unchanged points: an extrude past an open end, otherwise the midpoint of that curve segment on the
  // WORLD curve (what the viewport draws; the centripetal knots are not scale invariant).
  glm::vec3 newPointAfter(const Target &t, const Spline::Curve &curve, int a)
  {
    const auto &pts = t.data->points;
    const int n = (int)pts.size();
    if(n == 0)return glm::vec3{0.0f};
    if(n == 1)return pts[0].pos + glm::vec3{a < 0 ? -DEFAULT_STEP : DEFAULT_STEP, 0.0f, 0.0f};
    if(!curve.closed && a >= n - 1)return extrudeFrom(pts[(size_t)n - 1].pos, pts[(size_t)n - 2].pos);
    if(!curve.closed && a < 0)return extrudeFrom(pts[0].pos, pts[1].pos);

    const uint32_t seg = a < 0 ? (uint32_t)n - 1 : (uint32_t)a; // closed: -1 is the closing segment
    if(seg >= SM::segmentCount(curve.pointCount, curve.closed)) { // beyond the exported 64 points
      return (pts[(size_t)seg].pos + pts[((size_t)seg + 1) % pts.size()].pos) * 0.5f;
    }
    return t.xf.toLocal(toGlm(SM::evalSegment(curve.worldPts.data(), curve.pointCount, curve.closed, curve.type, seg, 0.5f)));
  }

  Placement shiftClickPlacement(int count, bool closedEff)
  {
    const int a = (session.anchor >= 0 && session.anchor < count) ? session.anchor : -1;
    if(count == 0)return {0, -1};
    if(a < 0 || (!closedEff && a == count - 1))return {count, count - 1}; // append
    if(!closedEff && a == 0)return {0, 0};                                // prepend (count >= 2 here)
    return {a + 1, a};                                                     // after the anchor
  }

  // --- projection ---------------------------------------------------------------------------------

  void projectPoints(const Target &t, const ViewInfo &vi, std::vector<ScreenPt> &out)
  {
    const auto &pts = t.data->points;
    const glm::vec2 size{vi.imageSize.x, vi.imageSize.y};
    out.resize(pts.size());
    for(size_t i = 0; i < pts.size(); ++i) {
      auto &sp = out[i];
      sp.visible = Editor::SceneRaycast::projectToScreen(t.xf.toWorld(pts[i].pos), size, *vi.view, *vi.proj, sp.pos, &sp.depth);
    }
  }

  void projectSamples(const std::vector<SM::Sample> &samples, const ViewInfo &vi, std::vector<ScreenPt> &out)
  {
    const glm::vec2 size{vi.imageSize.x, vi.imageSize.y};
    out.resize(samples.size());
    for(size_t i = 0; i < samples.size(); ++i) {
      auto &sp = out[i];
      sp.visible = Editor::SceneRaycast::projectToScreen(toGlm(samples[i]), size, *vi.view, *vi.proj, sp.pos, &sp.depth);
    }
  }

  int nearestHandle(const std::vector<ScreenPt> &handles, glm::vec2 mouse, float radius)
  {
    int best = -1;
    float bestDist = radius;
    for(size_t i = 0; i < handles.size(); ++i) {
      if(!handles[i].visible)continue;
      float dist = glm::length(handles[i].pos - mouse);
      if(dist <= bestDist) {
        bestDist = dist;
        best = (int)i;
      }
    }
    return best;
  }

  // Mouse distance to projected polyline piece i: sample i -> i + 1 (false if an end is hidden).
  bool measurePiece(const std::vector<ScreenPt> &proj, size_t i, glm::vec2 mouse, PieceHit &out)
  {
    const auto &a = proj[i];
    const auto &b = proj[i + 1];
    if(!a.visible || !b.visible)return false;

    glm::vec2 ab = b.pos - a.pos;
    float len2 = glm::dot(ab, ab);
    float t = len2 > 1e-8f ? std::clamp(glm::dot(mouse - a.pos, ab) / len2, 0.0f, 1.0f) : 0.0f;
    glm::vec2 p = a.pos + ab * t;
    out = {true, (uint32_t)i, t, glm::length(mouse - p), a.depth + (b.depth - a.depth) * t, p};
    return true;
  }

  // Nearest projected polyline piece within radius (pieces with a hidden end are skipped).
  PieceHit nearestPiece(const std::vector<ScreenPt> &proj, glm::vec2 mouse, float radius)
  {
    PieceHit best{};
    best.dist = radius;
    PieceHit hit{};
    for(size_t i = 0; i + 1 < proj.size(); ++i) {
      if(!measurePiece(proj, i, mouse, hit) || hit.dist > best.dist)continue;
      best = hit;
    }
    return best;
  }

  // Every piece within radius, nearest first (equal distances: the later piece first, as nearestPiece).
  void piecesWithin(const std::vector<ScreenPt> &proj, glm::vec2 mouse, float radius, std::vector<PieceHit> &out)
  {
    out.clear();
    PieceHit hit{};
    for(size_t i = 0; i + 1 < proj.size(); ++i) {
      if(measurePiece(proj, i, mouse, hit) && hit.dist <= radius)out.push_back(hit);
    }
    std::sort(out.begin(), out.end(), [](const PieceHit &a, const PieceHit &b) {
      return a.dist < b.dist || (a.dist == b.dist && a.piece > b.piece);
    });
  }

  Editor::SceneRaycast::Ray mouseRay(const ViewInfo &vi, glm::vec2 mouse)
  {
    return Editor::SceneRaycast::mouseRay(mouse, {vi.imageSize.x, vi.imageSize.y}, *vi.view, *vi.proj);
  }

  // Between the near and far plane (what the viewport can show): rejects grazing plane hits far
  // behind the horizon. Also right for ortho views, whose mouse rays start behind the camera.
  bool inViewDepth(const ViewInfo &vi, const glm::vec3 &world)
  {
    glm::vec2 px{};
    return Editor::SceneRaycast::projectToScreen(world, {vi.imageSize.x, vi.imageSize.y}, *vi.view, *vi.proj, px);
  }

  bool intersectInView(const ViewInfo &vi, const Editor::SceneRaycast::Ray &ray,
                       const glm::vec3 &planePoint, const glm::vec3 &planeNormal, glm::vec3 &out)
  {
    return Editor::SceneRaycast::intersectPlane(ray, planePoint, planeNormal, out) && inViewDepth(vi, out);
  }

  // Static geometry under the mouse, else the horizontal plane at planeY.
  bool surfaceUnderMouse(Project::Scene &scene, const ViewInfo &vi, glm::vec2 mouse, float planeY, glm::vec3 &out)
  {
    const auto ray = mouseRay(vi, mouse);
    if(Editor::SceneRaycast::raycastStaticMeshes(scene, ray, out))return true;
    return intersectInView(vi, ray, {0.0f, planeY, 0.0f}, {0.0f, 1.0f, 0.0f}, out);
  }

  // Drop to Floor height for one world point. It lands on the closest surface below it (cast from DROP_ON_EPS
  // above, so a point on or a hair inside a surface stays on it) if that surface's upper side is drawn: never on
  // an overhang above it. A culled side first (the inside of a closed mesh, e.g. sunk into a floor slab) or
  // nothing below (under the level) means the point is inside geometry: it goes up onto the closest surface
  // above it, at most DROP_LIFT. False: nowhere to go.
  bool floorHeight(Project::Scene &scene, const glm::vec3 &world, std::vector<Editor::SceneRaycast::Hit> &hits, float &outY)
  {
    using Editor::SceneRaycast::Hit;
    // nearest first; equally close: a culled side first
    auto order = [](std::vector<Hit> &v) {
      std::sort(v.begin(), v.end(), [](const Hit &a, const Hit &b) { return a.t < b.t || (a.t == b.t && !a.drawn && b.drawn); });
    };
    const glm::vec3 from{world.x, world.y + DROP_ON_EPS, world.z};
    Editor::SceneRaycast::raycastStaticMeshesAll(scene, {from, {0.0f, -1.0f, 0.0f}}, hits);
    order(hits);
    if(!hits.empty() && hits.front().drawn) { outY = from.y - hits.front().t; return true; }

    static std::vector<Hit> up{};
    Editor::SceneRaycast::raycastStaticMeshesAll(scene, {world, {0.0f, 1.0f, 0.0f}}, up);
    order(up);
    if(!hits.empty()) {
      // a culled side first below: inside a one-sided solid only if the nearest surface above shows the point
      // its culled (inner) side too; otherwise open geometry seen from behind: see through it, as the viewport
      if(!up.empty() && !up.front().drawn) {
        if(up.front().t > DROP_LIFT)return false;
        outY = world.y + up.front().t;
        return true;
      }
      for(const Hit &h : hits)if(h.drawn) { outY = from.y - h.t; return true; }
      return false;
    }
    // nothing below (under the level): up onto the nearest surface whose upper side is drawn
    for(const Hit &h : up) {
      if(h.t > DROP_LIFT)break;
      if(h.otherDrawn) { outY = world.y + h.t; return true; }
    }
    return false;
  }

  // --- viewport input state -----------------------------------------------------------------------

  // Drops a grab the point gizmo cannot continue: ImGuizmo's global mbUsing would block every gizmo.
  void dropGizmoGrab()
  {
    ImGuizmo::Enable(false);
    ImGuizmo::Enable(true);
  }

  void clearDragSnapshot(ViewportState &vs)
  {
    vs.dragIdx.clear();
    vs.dragStart.clear();
    vs.dragGrab = -1;
  }

  void clearPress(ViewportState &vs)
  {
    setDragging(vs.handleDragging, false);
    vs.handleArmed = false;
    vs.pressPending = false;
    vs.boxDragging = false;
    vs.pressHandle = -1;
    if(!vs.gizmoActive)clearDragSnapshot(vs); // the snapshot also belongs to a running gizmo drag
  }

  void endGizmoDrag(ViewportState &vs)
  {
    setDragging(vs.gizmoActive, false);
    if(!vs.handleArmed)clearDragSnapshot(vs);
  }

  // Puts the points of an uncommitted drag back (Esc, or this viewport stopped showing the edit).
  void restoreDrag(const ViewportState &vs, const Target &t)
  {
    auto &pts = t.data->points;
    if(vs.dragIdx.size() != vs.dragStart.size() || !indicesValid(vs.dragIdx, pts.size()))return;
    for(size_t k = 0; k < vs.dragIdx.size(); ++k) {
      pts[(size_t)vs.dragIdx[k]].pos = t.xf.toLocal(vs.dragStart[k]);
    }
  }

  // Arms a drag of the selection grabbed at handle `grabbed` (selected): the points follow the mouse
  // on the horizontal plane through the grabbed point, or a camera-facing one for flat mouse rays.
  void armHandleDrag(ViewportState &vs, const Target &t, const ViewInfo &vi, glm::vec2 mouse, int grabbed)
  {
    const auto &pts = t.data->points;
    clearPress(vs);
    vs.handleArmed = true;
    vs.pressHandle = grabbed;
    vs.pressPos = vs.curPos = mouse;

    vs.dragIdx = session.sel;
    if(!std::binary_search(vs.dragIdx.begin(), vs.dragIdx.end(), grabbed))vs.dragIdx = {grabbed};
    for(size_t k = 0; k < vs.dragIdx.size(); ++k) {
      vs.dragStart.push_back(t.xf.toWorld(pts[(size_t)vs.dragIdx[k]].pos));
      if(vs.dragIdx[k] == grabbed)vs.dragGrab = (int)k;
    }
    vs.dragGen = Editor::UndoRedo::getHistory().getGeneration();

    const glm::vec3 grabbedStart = vs.dragStart[(size_t)vs.dragGrab];
    const auto ray = mouseRay(vi, mouse);
    vs.planePoint = grabbedStart;
    if(std::abs(ray.dir.y) >= STEEP_RAY_Y) {
      vs.planeNormal = {0.0f, 1.0f, 0.0f};
    } else {
      vs.planeNormal = -glm::normalize(glm::vec3{glm::inverse(*vi.view)[2]});
    }
    glm::vec3 hit{};
    vs.grabOffset = Editor::SceneRaycast::intersectPlane(ray, vs.planePoint, vs.planeNormal, hit)
      ? grabbedStart - hit : glm::vec3{0.0f};
  }

  void updateHandleDrag(ViewportState &vs, const Target &t, const ViewInfo &vi, glm::vec2 mouse)
  {
    auto &pts = t.data->points;
    if(!indicesValid(vs.dragIdx, pts.size()) || vs.dragIdx.size() != vs.dragStart.size()
      || vs.dragGrab < 0 || (size_t)vs.dragGrab >= vs.dragStart.size())
    {
      clearPress(vs); // undo shrank the list mid-drag: stop and keep what the undo restored
      return;
    }

    glm::vec3 hit{};
    // no hit, or grazing the plane beyond the far plane: keep the last position
    if(!intersectInView(vi, mouseRay(vi, mouse), vs.planePoint, vs.planeNormal, hit))return;

    glm::vec3 target = hit + vs.grabOffset;
    if(ImGui::GetIO().KeyCtrl) {
      target.x = snapGrid(target.x);
      target.z = snapGrid(target.z);
      if(vs.planeNormal != glm::vec3{0.0f, 1.0f, 0.0f})target.y = snapGrid(target.y); // camera plane
    }

    const glm::vec3 delta = target - vs.dragStart[(size_t)vs.dragGrab];
    for(size_t k = 0; k < vs.dragIdx.size(); ++k) {
      pts[(size_t)vs.dragIdx[k]].pos = t.xf.toLocal(vs.dragStart[k] + delta);
    }
  }

  // Point gizmo on the selection. TRANSLATE only: ImGuizmo's scale is cumulative from the drag start,
  // so multi-point rotate/scale would compound. Returns true if the mouse is over (or using) it.
  bool drawPointGizmo(ImDrawList *dl, ViewportState &vs, const Target &t, const ViewInfo &vi)
  {
    auto &pts = t.data->points;

    // a running drag moves the points it grabbed; if they changed under it (undo), let go
    if(vs.gizmoActive && (vs.dragIdx != session.sel || vs.dragIdx.size() != vs.dragStart.size()
      || !indicesValid(vs.dragIdx, pts.size())))
    {
      dropGizmoGrab();
      endGizmoDrag(vs);
    }

    glm::vec3 centroid{0.0f};
    for(int i : session.sel)centroid += t.xf.toWorld(pts[(size_t)i].pos);
    centroid /= (float)session.sel.size();

    // World axes: a pure translation. (Not glm::identity<glm::quat>(): with GLM_FORCE_QUAT_DATA_XYZW
    // that is x=1, w=0, a 180 degree turn around X.) Local axes: the object's rotation.
    glm::mat4 gizmoMat = glm::translate(glm::mat4{1.0f}, centroid);
    if(!vi.worldSpace)gizmoMat *= glm::mat4_cast(t.xf.rot);
    glm::vec3 snap{SNAP_GRID};
    bool isSnap = ImGui::GetIO().KeyCtrl;

    // A gizmo origin behind the camera makes Manipulate() return before drawing, leaving its clip
    // rect pushed and IsOver() testing stale axes: detect that by the clip rect stack depth.
    // Ortho views (glm::ortho: [3][3] = 1) show their whole depth range, also behind the camera position:
    // tell ImGuizmo, or it skips that selection's gizmo. The flag is shared ImGuizmo state that the other
    // Manipulate() callers leave at its default, so it is only set around this call.
    const int clipDepth = dl->_ClipRectStack.Size;
    ImGuizmo::SetOrthographic((*vi.proj)[3][3] == 1.0f);
    const bool manipulated = ImGuizmo::Manipulate(
      glm::value_ptr(*vi.view),
      glm::value_ptr(*vi.proj),
      ImGuizmo::TRANSLATE,
      vi.worldSpace ? ImGuizmo::MODE::WORLD : ImGuizmo::MODE::LOCAL,
      glm::value_ptr(gizmoMat),
      nullptr,
      isSnap ? glm::value_ptr(snap) : nullptr
    );
    ImGuizmo::SetOrthographic(false);
    const bool drawn = dl->_ClipRectStack.Size == clipDepth;
    while(dl->_ClipRectStack.Size > clipDepth)dl->PopClipRect();

    // ImGuizmo also reports the first frame of every grab as a change (it compares against the
    // last delta of the previous drag): only an actual move starts a drag and its undo step
    const bool moved = manipulated && (vs.gizmoActive || glm::length(glm::vec3{gizmoMat[3]} - centroid) > 1e-3f);
    if(moved) {
      if(!vs.gizmoActive) {
        // snapshot at the first move: drift-free positions, and Esc can put them back
        clearDragSnapshot(vs);
        vs.dragIdx = session.sel;
        for(int i : vs.dragIdx)vs.dragStart.push_back(t.xf.toWorld(pts[(size_t)i].pos));
        vs.dragGen = Editor::UndoRedo::getHistory().getGeneration();
        setDragging(vs.gizmoActive, true);
      }

      glm::vec3 startCentroid{0.0f};
      for(auto &p : vs.dragStart)startCentroid += p;
      startCentroid /= (float)vs.dragStart.size();
      const glm::vec3 total = glm::vec3{gizmoMat[3]} - startCentroid;
      for(size_t k = 0; k < vs.dragIdx.size(); ++k) {
        pts[(size_t)vs.dragIdx[k]].pos = t.xf.toLocal(vs.dragStart[k] + total);
      }
    }

    const bool over = ImGuizmo::IsUsing() || (drawn && ImGuizmo::IsOver());
    if(ImGuizmo::IsUsing() && !vs.gizmoGrabbed) { // the grab: an undo from now on cancels it
      vs.gizmoGrabbed = true;
      if(!vs.gizmoActive)vs.dragGen = Editor::UndoRedo::getHistory().getGeneration();
    }

    // released inside Manipulate this frame: record the drag now (one undo step per drag)
    if(vs.gizmoActive && !ImGuizmo::IsUsing()) {
      commitTarget(t, "Move Spline Points");
      endGizmoDrag(vs);
    }
    return over;
  }

  void boxSelect(const ViewportState &vs, const std::vector<ScreenPt> &handles, glm::vec2 size, bool additive)
  {
    const glm::vec2 rMin = glm::clamp(glm::min(vs.pressPos, vs.curPos), glm::vec2{0.0f}, size);
    const glm::vec2 rMax = glm::clamp(glm::max(vs.pressPos, vs.curPos), glm::vec2{0.0f}, size);

    std::vector<int> sel = additive ? session.sel : std::vector<int>{};
    for(size_t i = 0; i < handles.size(); ++i) {
      const auto &h = handles[i];
      if(h.visible && h.pos.x >= rMin.x && h.pos.x <= rMax.x && h.pos.y >= rMin.y && h.pos.y <= rMax.y) {
        sel.push_back((int)i);
      }
    }
    normalizeIndices(sel);
    session.sel = std::move(sel);
    if(!std::binary_search(session.sel.begin(), session.sel.end(), session.anchor)) {
      session.anchor = session.sel.empty() ? -1 : session.sel.back();
    }
  }

  // Shift+Click: on the edited curve inserts a point there, otherwise adds one under the mouse (static
  // geometry, else the horizontal plane through the point it attaches to). Repeated clicks draw a path.
  void addPointAtMouse(ViewportState &vs, const Target &t, const ViewInfo &vi, glm::vec2 mouse,
                       const Spline::Curve &curve, const std::vector<ScreenPt> &curvePx)
  {
    auto &pts = t.data->points;
    if(!hasRoom(pts.size()))return;
    vs.ghostMouse = glm::vec2{-1.0f}; // the Shift ghost is stale after this

    const PieceHit onCurve = nearestPiece(curvePx, mouse, PICK_RADIUS * ImGui::Theme::zoomFactor);
    if(onCurve.hit && curve.pointCount >= 2 && curve.subdiv > 0) {
      uint32_t seg = 0;
      float u = 0.0f;
      SM::sampleToSegment(onCurve.piece, onCurve.t, curve.subdiv, seg, u);
      if(seg < SM::segmentCount(curve.pointCount, curve.closed)) {
        const auto world = SM::evalSegment(curve.worldPts.data(), curve.pointCount, curve.closed, curve.type, seg, u);
        const int at = (int)seg + 1;
        Spline::Point p{};
        p.pos = t.xf.toLocal(toGlm(world));
        pts.insert(pts.begin() + at, p);
        session.sel = {at};
        session.anchor = at;
        commitTarget(t, "Insert Spline Point");
        return;
      }
    }

    const int n = (int)pts.size();
    const auto settings = Spline::getSettings(*t.obj, *t.data);
    const Placement pl = shiftClickPlacement(n, SM::isClosed((uint32_t)n, settings.closed));
    const glm::vec3 ref = pl.ref >= 0 ? t.xf.toWorld(pts[(size_t)pl.ref].pos) : t.xf.pos;
    glm::vec3 world{};
    if(!surfaceUnderMouse(*t.scene, vi, mouse, ref.y, world))return;

    Spline::Point p{};
    p.pos = t.xf.toLocal(world);
    pts.insert(pts.begin() + pl.insertAt, p);
    session.sel = {pl.insertAt};
    session.anchor = pl.insertAt;
    commitTarget(t, "Add Spline Point");
  }

  // Shift+S: snaps the selected (or all) points to the world grid.
  bool snapPoints(uint32_t objUUID, uint64_t compUUID, std::vector<int> indices)
  {
    Target t{};
    if(!resolveForEdit(objUUID, compUUID, t))return false;
    auto &pts = t.data->points;
    if(indices.empty()) {
      indices.resize(pts.size());
      std::iota(indices.begin(), indices.end(), 0);
    }

    bool changed = false;
    for(int i : indices) {
      if(i < 0 || (size_t)i >= pts.size())continue;
      glm::vec3 world = t.xf.toWorld(pts[(size_t)i].pos);
      world = {snapGrid(world.x), snapGrid(world.y), snapGrid(world.z)};
      glm::vec3 local = t.xf.toLocal(world);
      if(local == pts[(size_t)i].pos)continue;
      pts[(size_t)i].pos = local;
      changed = true;
    }
    if(changed)commitTarget(t, "Snap Spline Points");
    return changed;
  }

  // --- keys ---------------------------------------------------------------------------------------

  bool keysAllowed(const ViewportState &vs)
  {
    const auto &io = ImGui::GetIO();
    return ImGui::IsWindowFocused() && !io.WantTextInput
      && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)
      && !vs.handleArmed && !vs.pressPending && !vs.boxDragging && !vs.handleDragging
      && !vs.gizmoActive && !ImGuizmo::IsUsing() && !dragBlocked();
  }

  // Esc: cancel the drag / box, else clear the point selection, else leave point edit.
  void escapeCascade(ViewportState &vs, const Target &t)
  {
    const bool lmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if(vs.handleDragging) {
      restoreDrag(vs, t);
      clearPress(vs);
      vs.ignoreUntilRelease = lmbDown;
    } else if(vs.gizmoActive || ImGuizmo::IsUsing()) {
      dropGizmoGrab();
      if(vs.gizmoActive)restoreDrag(vs, t);
      endGizmoDrag(vs);
      vs.ignoreUntilRelease = lmbDown;
    } else if(vs.handleArmed || vs.pressPending || vs.boxDragging) {
      clearPress(vs);
      vs.ignoreUntilRelease = lmbDown;
    } else if(!session.sel.empty()) {
      Editor::SplineEdit::clearSelection();
    } else {
      Editor::SplineEdit::end();
    }
  }

  // Focused viewport only, no key repeat. `edit` = the editable session target (nullptr: object mode).
  void handleKeys(ViewportState &vs, const Target *edit)
  {
    const auto &io = ImGui::GetIO();
    if(edit && ImGui::IsWindowFocused() && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      escapeCascade(vs, *edit);
      return;
    }
    if(!keysAllowed(vs))return;

    const auto &km = ctx.prefs.keymap;
    const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeySuper; // Shift stays usable while drawing
    if(plain && ImGui::IsKeyPressed(km.splineEditToggle, false)) {
      Editor::SplineEdit::toggleForSelection();
      return;
    }
    if(!edit)return;

    const uint32_t obj = session.objUUID;
    const uint64_t comp = session.compUUID;
    if(ImGui::IsKeyDown(ImGuiKey_LeftShift) && !io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(km.snapObject, false)) {
      snapPoints(obj, comp, session.sel);
    } else if(plain && ImGui::IsKeyPressed(km.deleteObject, false)) {
      // always consumed in point edit: never deletes the object
      if(!session.sel.empty())Editor::SplineEdit::removePoints(obj, comp, session.sel);
    } else if(plain && ImGui::IsKeyPressed(km.splineExtrude, false)) {
      Editor::SplineEdit::extrude(obj, comp);
    } else if(plain && ImGui::IsKeyPressed(km.splineInsertPoint, false)) {
      Editor::SplineEdit::insertAfter(obj, comp, session.sel);
    } else if(ImGui::IsKeyChordPressed(km.splineSelectAll)) {
      Editor::SplineEdit::selectAll();
    }
  }

  // Clean preview (no overlays): the points are not shown, so no key edits them. Esc and the toggle key
  // still leave point edit (directly, no Esc cascade); the toggle never starts a session here.
  void handleCleanPreviewKeys(const ViewportState &vs)
  {
    if(!session.active || !keysAllowed(vs))return;
    const auto &io = ImGui::GetIO();
    const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
    if(ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (plain && ImGui::IsKeyPressed(ctx.prefs.keymap.splineEditToggle, false))) {
      Editor::SplineEdit::end();
    }
  }

  // --- overlay ------------------------------------------------------------------------------------

  // Point gizmo id nested under the viewport's PushID(winId): the object gizmo never inherits a point drag.
  struct GizmoIdScope
  {
    GizmoIdScope() { ImGuizmo::PushID("SplinePts"); }
    ~GizmoIdScope() { ImGuizmo::PopID(); }
    GizmoIdScope(const GizmoIdScope&) = delete;
    GizmoIdScope& operator=(const GizmoIdScope&) = delete;
  };

  // Overlay clipped to the image, in two layers: 0 = curve/handles/labels, 1 = point gizmo/banner.
  // Handles are drawn after this frame's edits (no lag behind the gizmo) but still below it.
  struct OverlayLayers
  {
    ImDrawList *dl;

    explicit OverlayLayers(const ViewInfo &vi) : dl{ImGui::GetWindowDrawList()}
    {
      dl->PushClipRect(vi.imageMin, ImVec2{vi.imageMin.x + vi.imageSize.x, vi.imageMin.y + vi.imageSize.y}, true);
      overlaySplitter.Split(dl, 2);
    }
    ~OverlayLayers()
    {
      overlaySplitter.Merge(dl);
      dl->PopClipRect();
    }
    OverlayLayers(const OverlayLayers&) = delete;
    OverlayLayers& operator=(const OverlayLayers&) = delete;

    void top() { overlaySplitter.SetCurrentChannel(dl, 1); }
    void bottom() { overlaySplitter.SetCurrentChannel(dl, 0); }
  };

  ImVec2 toScreen(const ViewInfo &vi, glm::vec2 p) { return {vi.imageMin.x + p.x, vi.imageMin.y + p.y}; }

  // Edit mode: the curve on top of the geometry (the 3D preview line is depth tested).
  void drawCurveOverlay(ImDrawList *dl, const ViewInfo &vi, const std::vector<ScreenPt> &curvePx)
  {
    static std::vector<ImVec2> run{};
    const glm::vec4 &c = ctx.prefs.splineLineColorSelected;
    const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4{c.r, c.g, c.b, 0.35f});
    const float thickness = 1.5f * ImGui::Theme::zoomFactor;

    run.clear();
    auto flush = [&]() {
      if(run.size() >= 2)dl->AddPolyline(run.data(), (int)run.size(), col, ImDrawFlags_None, thickness);
      run.clear();
    };
    for(auto &p : curvePx) {
      if(!p.visible) { // broken at samples behind the camera
        flush();
        continue;
      }
      run.push_back(toScreen(vi, p.pos));
    }
    flush();
  }

  void drawHandles(ImDrawList *dl, const ViewInfo &vi, const Target &t, const std::vector<ScreenPt> &handles,
                   bool editMode, int hover)
  {
    static std::vector<int> order{};
    const float zoom = ImGui::Theme::zoomFactor;
    const float r = HANDLE_RADIUS * zoom;
    const bool rowHover = session.hoverRow >= 0 && ImGui::GetFrameCount() - session.hoverRowFrame <= 1;

    order.clear();
    for(size_t i = 0; i < handles.size(); ++i) {
      if(handles[i].visible)order.push_back((int)i);
    }
    std::sort(order.begin(), order.end(), [&handles](int a, int b) { // far to near
      return handles[(size_t)a].depth > handles[(size_t)b].depth;
    });

    char label[48];
    for(int i : order) {
      const ImVec2 c = toScreen(vi, handles[(size_t)i].pos);
      const float rad = (i == hover) ? r * 1.4f : r;

      if(!editMode) {
        const ImU32 col = !t.editable ? COL_LOCKED : (i == 0 ? COL_START : COL_PASSIVE);
        dl->AddCircle(c, rad, col, 0, 1.5f * zoom);
      } else {
        dl->AddCircleFilled(c, rad, Editor::SplineEdit::isPointSelected(i) ? COL_SELECTED : COL_FILL);
        if(i == 0) {
          dl->AddCircle(c, rad, COL_START, 0, 2.0f * zoom);
        } else {
          dl->AddCircle(c, rad, COL_OUTLINE, 0, 1.0f * zoom);
        }
      }
      if(rowHover && i == session.hoverRow)dl->AddCircle(c, rad + 4.0f * zoom, COL_ROW_HOVER, 0, 2.0f * zoom);

      if(editMode) {
        const uint8_t waitDs = t.data->points[(size_t)i].waitDs;
        if(waitDs > 0) {
          snprintf(label, sizeof(label), "%d " ICON_MDI_TIMER_OUTLINE "%.1fs", i, (double)waitDs / 10.0);
        } else {
          snprintf(label, sizeof(label), "%d", i);
        }
        const ImVec2 lp{c.x + r + 2.0f * zoom, c.y - r - 10.0f * zoom};
        dl->AddText(ImVec2{lp.x + 1.0f, lp.y + 1.0f}, COL_SHADOW, label);
        dl->AddText(lp, COL_LABEL, label);
      }
    }
  }

  // Shift held: where a click would add a point. On the curve a green "+", otherwise a faded line
  // from the point(s) it attaches to and a hollow handle. The brute-force static mesh raycast is
  // cached until the mouse or the camera moves; the plane fallback follows anchor changes.
  void drawShiftGhost(ImDrawList *dl, const ViewInfo &vi, ViewportState &vs, const Target &t, glm::vec2 mouse,
                      const std::vector<ScreenPt> &handles, const std::vector<ScreenPt> &curvePx)
  {
    const float zoom = ImGui::Theme::zoomFactor;
    const PieceHit onCurve = nearestPiece(curvePx, mouse, PICK_RADIUS * zoom);
    if(onCurve.hit) {
      const ImVec2 c = toScreen(vi, onCurve.pos);
      const float s = 6.0f * zoom;
      dl->AddLine(ImVec2{c.x - s, c.y}, ImVec2{c.x + s, c.y}, COL_GHOST_ADD, 2.0f * zoom);
      dl->AddLine(ImVec2{c.x, c.y - s}, ImVec2{c.x, c.y + s}, COL_GHOST_ADD, 2.0f * zoom);
      return;
    }

    const auto &pts = t.data->points;
    const int n = (int)pts.size();
    const bool closedEff = SM::isClosed((uint32_t)n, Spline::getSettings(*t.obj, *t.data).closed);
    const Placement pl = shiftClickPlacement(n, closedEff);

    const glm::mat4 viewProj = *vi.proj * *vi.view;
    if(vs.ghostMouse != mouse || vs.ghostViewProj != viewProj) {
      vs.ghostHit = Editor::SceneRaycast::raycastStaticMeshes(*t.scene, mouseRay(vi, mouse), vs.ghostPos);
      vs.ghostMouse = mouse;
      vs.ghostViewProj = viewProj;
    }

    glm::vec3 ghost = vs.ghostPos;
    if(!vs.ghostHit) {
      const glm::vec3 ref = pl.ref >= 0 ? t.xf.toWorld(pts[(size_t)pl.ref].pos) : t.xf.pos;
      if(!intersectInView(vi, mouseRay(vi, mouse), {0.0f, ref.y, 0.0f}, {0.0f, 1.0f, 0.0f}, ghost))return;
    }

    glm::vec2 gp{};
    if(!Editor::SceneRaycast::projectToScreen(ghost, {vi.imageSize.x, vi.imageSize.y}, *vi.view, *vi.proj, gp))return;
    const ImVec2 g = toScreen(vi, gp);

    // attaches to `ref`, and inside a path (or a loop) also to the point that will follow it
    int next = pl.insertAt < n ? pl.insertAt : (closedEff ? 0 : -1);
    if(next == pl.ref)next = -1;
    for(int idx : {pl.ref, next}) {
      if(idx < 0 || (size_t)idx >= handles.size() || !handles[(size_t)idx].visible)continue;
      dl->AddLine(toScreen(vi, handles[(size_t)idx].pos), g, COL_GHOST, 1.5f * zoom);
    }
    dl->AddCircle(g, HANDLE_RADIUS * zoom, COL_GHOST, 0, 1.5f * zoom);
  }

  void drawBanner(ImDrawList *dl, const ViewInfo &vi, const Target &t)
  {
    const auto &km = ctx.prefs.keymap;
    const float zoom = ImGui::Theme::zoomFactor;

    const std::string line1 = std::string{ICON_MDI_VECTOR_CURVE "  SPLINE EDIT · "} + t.obj->name + " / " + t.entry->name
      + " · " + std::to_string(session.sel.size()) + "/" + std::to_string(t.data->points.size()) + " selected · Esc: done";
    const std::string line2 = std::string{"Click: select · Drag: move · Shift+Click: add · "}
      + ImGui::GetKeyName(km.deleteObject) + ": delete · "
      + ImGui::GetKeyName(km.splineInsertPoint) + ": insert · "
      + ImGui::GetKeyName(km.splineExtrude) + ": extrude";
    const char *line3 = t.templateOwned ? "(prefab: affects every instance, not undoable)" : nullptr;

    const ImVec2 size1 = ImGui::CalcTextSize(line1.c_str());
    const ImVec2 size2 = ImGui::CalcTextSize(line2.c_str());
    const ImVec2 size3 = line3 ? ImGui::CalcTextSize(line3) : ImVec2{0.0f, 0.0f};
    const float lineH = ImGui::GetTextLineHeight();
    const float pad = 6.0f * zoom;
    const float width = std::max({size1.x, size2.x, size3.x}) + pad * 2.0f;
    const float height = lineH * (line3 ? 3.0f : 2.0f) + pad * 2.0f;

    const float centerX = vi.imageMin.x + vi.imageSize.x * 0.5f;
    const ImVec2 bMin{centerX - width * 0.5f, vi.imageMin.y + 8.0f * zoom};
    dl->AddRectFilled(bMin, ImVec2{bMin.x + width, bMin.y + height}, COL_BANNER_BG, 6.0f * zoom);

    float y = bMin.y + pad;
    dl->AddText(ImVec2{centerX - size1.x * 0.5f, y}, COL_BANNER_TXT, line1.c_str());
    y += lineH;
    dl->AddText(ImVec2{centerX - size2.x * 0.5f, y}, COL_BANNER_DIM, line2.c_str());
    y += lineH;
    if(line3)dl->AddText(ImVec2{centerX - size3.x * 0.5f, y}, COL_BANNER_WARN, line3);
  }

  // --- curve pick (object mode) -------------------------------------------------------------------

  struct CurvePick
  {
    Project::Scene &scene;
    const ViewInfo &vi;
    glm::vec2 mouse{};
    float radius{0.0f};
    uint32_t uuid{0};
    float dist{0.0f};
    float depth{0.0f};
    Spline::Curve curve{};
    std::vector<ScreenPt> proj{};
    std::vector<PieceHit> pieces{};
  };

  // The preview line is depth tested (compSpline draw3D: a box `thickness` wide through the samples lifted
  // by thickness / 2 + 0.25): true if static geometry hides it at the pixel of `hit`. Tested along the ray
  // through the line's own pixel, so the surface the line lies on never hides it.
  bool curveHiddenAt(const CurvePick &pick, const PieceHit &hit, float thickness)
  {
    const auto &samples = pick.curve.samples;
    const glm::vec3 lift{0.0f, thickness * 0.5f + 0.25f, 0.0f};
    const glm::vec3 a = toGlm(samples[hit.piece]) + lift;
    const glm::vec3 e = toGlm(samples[(size_t)hit.piece + 1]) + lift - a;
    const auto ray = mouseRay(pick.vi, hit.pos);

    // the point of the piece on that ray: closest points of the two lines (|dir| = 1; seen end-on, the
    // screen-space fraction)
    const glm::vec3 w = ray.origin - a;
    const float de = glm::dot(ray.dir, e);
    const float ee = glm::dot(e, e);
    const float denom = ee - de * de;
    float s = hit.t;
    if(denom > 1e-6f * ee)s = std::clamp((glm::dot(e, w) - de * glm::dot(ray.dir, w)) / denom, 0.0f, 1.0f);
    const float tLine = glm::dot(a + e * s - ray.origin, ray.dir);

    // geometry in front of the line's near side hides it
    const float tFront = tLine - thickness * 0.5f;
    glm::vec3 occluder{};
    return tFront > 0.0f && Editor::SceneRaycast::raycastStaticMeshes(pick.scene, ray, occluder, tFront);
  }

  void pickCurveIn(Project::Object &parent, CurvePick &pick)
  {
    auto &assets = ctx.project->getAssets();
    for(auto &child : parent.children)
    {
      if(!child->enabled)continue; // hides the subtree, like the viewport's iterateObjects

      auto srcObj = child.get();
      if(child->isPrefabInstance()) {
        auto *node = assets.getPrefabSourceNode(*child);
        if(node)srcObj = node;
      }

      if(child->selectable) {
        auto test = [&](Project::Component::Entry &comp) {
          if(comp.id != Spline::ID || !comp.data)return;
          Spline::buildCurve(*child, Spline::getData(comp), assets, pick.curve);
          projectSamples(pick.curve.samples, pick.vi, pick.proj);
          piecesWithin(pick.proj, pick.mouse, pick.radius, pick.pieces);

          // compSpline draw3D's line width
          const bool selected = Editor::SplineEdit::isEditing(child->uuid, comp.uuid) || ctx.isObjectSelected(child->uuid);
          const float thickness = std::max(ctx.prefs.splineLineThickness, 0.0f) * (selected ? 1.6f : 1.0f);

          // this curve's nearest piece the viewport shows
          for(const PieceHit &hit : pick.pieces) {
            if(pick.uuid != 0 && hit.dist > pick.dist + PICK_TIE_PX)return; // no piece left that can win
            if(curveHiddenAt(pick, hit, thickness))continue;

            const bool better = pick.uuid == 0 || hit.dist < pick.dist - PICK_TIE_PX
              || (hit.dist <= pick.dist + PICK_TIE_PX && hit.depth < pick.depth);
            if(better) {
              pick.uuid = child->uuid;
              pick.dist = hit.dist;
              pick.depth = hit.depth;
            }
            return;
          }
        };
        // template components first, then the ones added directly on a prefab instance
        for(auto &comp : srcObj->components)test(comp);
        if(srcObj != child.get()) {
          for(auto &comp : child->components)test(comp);
        }
      }

      pickCurveIn(*child, pick);
    }
  }
}

namespace Editor::SplineEdit
{
  // --- session -----------------------------------------------------------------------------------

  bool isEditing()
  {
    return session.active;
  }

  bool isEditing(uint32_t objUUID, uint64_t compUUID)
  {
    return isSessionTarget(objUUID, compUUID);
  }

  bool begin(uint32_t objUUID, uint64_t compUUID, int selectPoint)
  {
    Target t{};
    if(!resolve(objUUID, compUUID, t))return false;
    if(!t.editable) {
      Noti::add(Noti::ERROR, "Spline points of '" + t.obj->name
        + "' belong to its prefab. Click 'Edit' on the prefab instance to change them.");
      return false;
    }

    session = Session{};
    session.active = true;
    session.objUUID = objUUID;
    session.compUUID = compUUID;
    if(selectPoint >= 0 && (size_t)selectPoint < t.data->points.size()) {
      session.sel = {selectPoint};
      session.anchor = selectPoint;
    }
    return true;
  }

  void end()
  {
    session = Session{};
  }

  void validate(Project::Scene *scene)
  {
    if(!session.active)return;

    // the selection is editor state: selecting something else ends point edit even while the
    // game builds or runs (which can take a while)
    if(ctx.selObjectUUIDs.size() != 1 || ctx.selObjectUUID != session.objUUID) {
      end();
      return;
    }
    if(ctx.isBuildOrRunning())return; // the async build points ctx.project at its own copy

    Target t{};
    if(!resolveIn(scene, session.objUUID, session.compUUID, t) || !t.editable) {
      end();
      return;
    }
    clampSession(t.data->points.size());
  }

  bool toggleForSelection()
  {
    if(session.active) {
      end();
      return true;
    }
    Target t{};
    if(!resolveSelectedSpline(t))return false;
    return begin(t.obj->uuid, t.entry->uuid);
  }

  // --- point selection ---------------------------------------------------------------------------

  const std::vector<int>& selection()
  {
    static const std::vector<int> none{};
    return session.active ? session.sel : none;
  }

  bool isPointSelected(int idx)
  {
    return session.active && std::binary_search(session.sel.begin(), session.sel.end(), idx);
  }

  void selectPoint(int idx, bool toggle)
  {
    Target t{};
    if(!session.active || idx < 0 || !resolve(session.objUUID, session.compUUID, t))return;
    if((size_t)idx >= t.data->points.size())return;
    if(!toggle) {
      session.sel = {idx};
      session.anchor = idx;
      return;
    }

    auto it = std::lower_bound(session.sel.begin(), session.sel.end(), idx);
    if(it != session.sel.end() && *it == idx) {
      session.sel.erase(it);
      if(session.anchor == idx)session.anchor = session.sel.empty() ? -1 : session.sel.back();
    } else {
      session.sel.insert(it, idx);
      session.anchor = idx;
    }
  }

  void selectAll()
  {
    Target t{};
    if(!session.active || !resolve(session.objUUID, session.compUUID, t))return;
    const size_t count = t.data->points.size();
    clampSession(count);
    if(session.sel.size() == count) {
      clearSelection();
      return;
    }
    session.sel.resize(count);
    std::iota(session.sel.begin(), session.sel.end(), 0);
    if(session.anchor < 0)session.anchor = (int)count - 1;
  }

  void clearSelection()
  {
    session.sel.clear();
    session.anchor = -1;
  }

  void setInspectorHover(int idx)
  {
    session.hoverRow = idx;
    session.hoverRowFrame = ImGui::GetFrameCount();
  }

  // --- queries -----------------------------------------------------------------------------------

  bool canEditPoints(uint32_t objUUID, uint64_t compUUID)
  {
    Target t{};
    return resolve(objUUID, compUUID, t) && t.editable;
  }

  bool getFocusBounds(glm::vec3 &center, float &radius)
  {
    // false without points: the viewport then frames the object itself
    Target t{};
    if(!session.active || !resolve(session.objUUID, session.compUUID, t))return false;
    const auto &pts = t.data->points;
    if(pts.empty())return false;
    clampSession(pts.size());

    glm::vec3 bMin{std::numeric_limits<float>::max()};
    glm::vec3 bMax{-std::numeric_limits<float>::max()};
    auto add = [&](size_t i) {
      const glm::vec3 w = t.xf.toWorld(pts[i].pos);
      bMin = glm::min(bMin, w);
      bMax = glm::max(bMax, w);
    };
    if(!session.sel.empty()) {
      for(int i : session.sel)add((size_t)i);
    } else {
      for(size_t i = 0; i < pts.size(); ++i)add(i);
    }

    center = (bMin + bMax) * 0.5f;
    radius = std::max(glm::length((bMax - bMin) * 0.5f), 50.0f);
    return true;
  }

  // --- operations ---------------------------------------------------------------------------------

  bool extrude(uint32_t objUUID, uint64_t compUUID)
  {
    Target t{};
    if(!resolveForEdit(objUUID, compUUID, t))return false;
    auto &pts = t.data->points;
    if(!hasRoom(pts.size()))return false;
    const int n = (int)pts.size();

    int a = n - 1;
    if(isSessionTarget(objUUID, compUUID)) {
      clampSession(pts.size());
      if(session.anchor >= 0)a = session.anchor;
      else if(!session.sel.empty())a = session.sel.back();
    }

    Spline::Curve curve{};
    Spline::buildCurve(*t.obj, *t.data, ctx.project->getAssets(), curve);

    int at = a + 1;
    glm::vec3 pos{0.0f};
    if(n == 0) {
      at = 0; // the local origin
    } else if(n == 1) {
      at = 1;
      pos = pts[0].pos + glm::vec3{DEFAULT_STEP, 0.0f, 0.0f};
    } else if(!curve.closed && a == n - 1) {
      at = n;
      pos = extrudeFrom(pts[(size_t)n - 1].pos, pts[(size_t)n - 2].pos);
    } else if(!curve.closed && a == 0) {
      at = 0;
      pos = extrudeFrom(pts[0].pos, pts[1].pos);
    } else {
      pos = newPointAfter(t, curve, a); // curve midpoint after the anchor
    }

    Spline::Point p{};
    p.pos = pos;
    pts.insert(pts.begin() + at, p);
    selectOnly(objUUID, compUUID, at);
    commitTarget(t, "Add Spline Point");
    return true;
  }

  bool insertAfter(uint32_t objUUID, uint64_t compUUID, std::vector<int> after)
  {
    Target t{};
    if(!resolveForEdit(objUUID, compUUID, t))return false;
    auto &pts = t.data->points;
    if(!hasRoom(pts.size()))return false;
    const int n = (int)pts.size();

    if(after.empty())after.push_back(n - 1); // append (the first point of an empty spline)
    for(int &a : after)a = std::clamp(a, -1, n - 1);
    normalizeIndices(after);
    const size_t room = Spline::MAX_POINTS - pts.size();
    if(after.size() > room) {
      after.resize(room);
      Noti::add(Noti::ERROR, "A spline can have at most " + std::to_string(Spline::MAX_POINTS) + " points");
    }

    Spline::Curve curve{};
    Spline::buildCurve(*t.obj, *t.data, ctx.project->getAssets(), curve);

    // positions from the ORIGINAL points, then insert back to front so earlier indices stay valid
    std::vector<glm::vec3> newPos{};
    newPos.reserve(after.size());
    for(int a : after)newPos.push_back(newPointAfter(t, curve, a));
    for(size_t k = after.size(); k-- > 0;) {
      Spline::Point p{};
      p.pos = newPos[k];
      pts.insert(pts.begin() + (after[k] + 1), p);
    }

    if(isSessionTarget(objUUID, compUUID)) {
      session.sel.clear();
      for(size_t k = 0; k < after.size(); ++k)session.sel.push_back(after[k] + 1 + (int)k);
      session.anchor = session.sel.back();
    }
    commitTarget(t, "Insert Spline Point");
    return true;
  }

  bool removePoints(uint32_t objUUID, uint64_t compUUID, std::vector<int> indices)
  {
    Target t{};
    if(!resolveForEdit(objUUID, compUUID, t))return false;
    auto &pts = t.data->points;
    std::erase_if(indices, [&pts](int i) { return i < 0 || (size_t)i >= pts.size(); });
    normalizeIndices(indices);
    if(indices.empty())return false;

    for(size_t k = indices.size(); k-- > 0;)pts.erase(pts.begin() + indices[k]);

    // keep a neighbour selected so repeated deletes walk along the path
    selectOnly(objUUID, compUUID, std::min(indices.front() > 0 ? indices.front() - 1 : 0, (int)pts.size() - 1));
    commitTarget(t, "Delete Spline Points");
    return true;
  }

  bool reversePoints(uint32_t objUUID, uint64_t compUUID)
  {
    Target t{};
    if(!resolveForEdit(objUUID, compUUID, t))return false;
    auto &pts = t.data->points;
    if(pts.size() < 2)return false;

    std::reverse(pts.begin(), pts.end()); // waits stay with their points
    if(isSessionTarget(objUUID, compUUID)) {
      const int n = (int)pts.size();
      clampSession(pts.size());
      for(int &i : session.sel)i = n - 1 - i;
      normalizeIndices(session.sel);
      if(session.anchor >= 0)session.anchor = n - 1 - session.anchor;
    }
    commitTarget(t, "Reverse Spline");
    return true;
  }

  bool setWait(uint32_t objUUID, uint64_t compUUID, const std::vector<int> &indices, uint8_t waitDs)
  {
    Target t{};
    if(!resolveForEdit(objUUID, compUUID, t))return false;
    auto &pts = t.data->points;

    bool any = false;
    for(int i : indices) {
      if(i < 0 || (size_t)i >= pts.size())continue;
      pts[(size_t)i].waitDs = waitDs;
      any = true;
    }
    if(any)commitTarget(t, "Set Spline Wait");
    return any;
  }

  int dropToFloor(uint32_t objUUID, uint64_t compUUID, std::vector<int> indices)
  {
    Target t{};
    if(!resolveForEdit(objUUID, compUUID, t))return 0;
    auto &pts = t.data->points;
    if(indices.empty()) {
      indices.resize(pts.size());
      std::iota(indices.begin(), indices.end(), 0);
    } else {
      std::erase_if(indices, [&pts](int i) { return i < 0 || (size_t)i >= pts.size(); });
      normalizeIndices(indices);
    }

    std::vector<SceneRaycast::Hit> hits{};
    int moved = 0;
    for(int i : indices) {
      auto &p = pts[(size_t)i];
      const glm::vec3 world = t.xf.toWorld(p.pos);
      float floorY = 0.0f;
      if(!floorHeight(*t.scene, world, hits, floorY))continue;

      const glm::vec3 local = t.xf.toLocal({world.x, floorY, world.z});
      if(glm::length(local - p.pos) <= 1e-3f)continue; // already on the floor
      p.pos = local;
      ++moved;
    }
    if(moved > 0)commitTarget(t, "Drop Spline Points");
    return moved;
  }

  void commit(uint32_t objUUID, uint64_t compUUID, const char *reason)
  {
    Target t{};
    if(!resolve(objUUID, compUUID, t))return;
    commitTarget(t, reason ? reason : "Edit Spline");
  }

  // --- viewport hooks ----------------------------------------------------------------------------

  bool wantsMouse(const ViewInfo &vi, glm::vec2 mouseLocal)
  {
    if(vi.overlaysHidden || !vi.view || !vi.proj)return false;
    if(session.active)return vi.hovered; // point edit owns every left click on the image

    // passive handles of the single selected spline: a click on one enters point edit. The object gizmo is
    // drawn over them and keeps its clicks.
    if(!vi.hovered || vi.mouseBlocked || vi.objectGizmoHovered || !vi.showSplines)return false;
    Target t{};
    if(!resolveSelectedSpline(t) || !t.editable)return false;

    static std::vector<ScreenPt> handles{};
    projectPoints(t, vi, handles);
    return nearestHandle(handles, mouseLocal, PICK_RADIUS * ImGui::Theme::zoomFactor) >= 0;
  }

  void drawViewport(const ViewInfo &vi, ViewportState &vs)
  {
    static std::vector<ScreenPt> handles{};
    static std::vector<ScreenPt> curvePx{};
    static Spline::Curve curve{};

    GizmoIdScope gizmoId{}; // 1.
    const auto &io = ImGui::GetIO();
    const bool haveMatrices = vi.view && vi.proj;

    // 2. targets, re-resolved every frame
    Target t{};
    const bool resolved = session.active && resolve(session.objUUID, session.compUUID, t);
    bool editMode = resolved && t.editable;
    if(editMode)clampSession(t.data->points.size());

    Target pt{};
    bool passive = !session.active && vi.showSplines && resolveSelectedSpline(pt);

    // 3. an undo/redo since the press (every Data is new, its points may differ): cancel the press or drag
    //    without recording or restoring anything (the undo put its own points in), mouse ignored until released
    if(!ImGuizmo::IsUsing())vs.gizmoGrabbed = false;
    if((vs.handleArmed || vs.handleDragging || vs.gizmoActive || vs.gizmoGrabbed)
      && vs.dragGen != Editor::UndoRedo::getHistory().getGeneration())
    {
      if(vs.gizmoActive || vs.gizmoGrabbed)dropGizmoGrab();
      vs.gizmoGrabbed = false;
      endGizmoDrag(vs);
      clearPress(vs);
      vs.ignoreUntilRelease = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    }

    // ... the drop-grab rule, and the end of a gizmo drag whose gizmo is no longer drawn
    const bool willGizmo = editMode && haveMatrices && !vi.overlaysHidden && !session.sel.empty()
      && !vs.handleArmed && !vs.handleDragging;
    if(ImGuizmo::IsUsing() && !willGizmo)dropGizmoGrab();
    if(vs.gizmoActive && !ImGuizmo::IsUsing()) {
      if(editMode)commitTarget(t, "Move Spline Points");
      endGizmoDrag(vs);
    }
    if(vs.handleDragging || vs.gizmoActive)dragSeenFrame = ImGui::GetFrameCount();

    // 4. nothing to show here: forget the press (a live handle drag puts its points back first)
    if(vi.overlaysHidden || !haveMatrices || (!editMode && !passive)) {
      if(vs.handleDragging && resolved)restoreDrag(vs, t);
      clearPress(vs);
      vs.ignoreUntilRelease = false;
      vs.ghostMouse = glm::vec2{-1.0f};
      if(!vi.overlaysHidden) {
        handleKeys(vs, nullptr); // Tab/P also works on a hidden or locked spline
      } else {
        handleCleanPreviewKeys(vs);
      }
      return;
    }

    const Target *cur = editMode ? &t : &pt;
    const glm::vec2 size{vi.imageSize.x, vi.imageSize.y};
    const ImVec2 mouseAbs = ImGui::GetMousePos(); // Viewport3D::mousePos is zeroed at this point
    const glm::vec2 mouse{mouseAbs.x - vi.imageMin.x, mouseAbs.y - vi.imageMin.y};
    const float pickR = PICK_RADIUS * ImGui::Theme::zoomFactor;
    const bool lmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if(!lmbDown)vs.ignoreUntilRelease = false;

    // 5. projection and hover (passive handles: not under the object gizmo, it is drawn over them)
    projectPoints(*cur, vi, handles);
    const bool canHover = vi.hovered && !vi.mouseBlocked && cur->editable && (editMode || !vi.objectGizmoHovered);
    int hover = canHover ? nearestHandle(handles, mouse, pickR) : -1;
    if(editMode) {
      Spline::buildCurve(*t.obj, *t.data, ctx.project->getAssets(), curve);
      projectSamples(curve.samples, vi, curvePx);
    }

    OverlayLayers layers{vi};
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && vi.hovered && !vi.mouseBlocked
      && !vs.ignoreUntilRelease;

    // 7. press on a handle: the handle ALWAYS wins over the point gizmo (skipped for the whole drag)
    bool handlePressed = false;
    if(clicked && hover >= 0) {
      handlePressed = true;
      if(!editMode) {
        // passive handle: enter point edit on that point and allow dragging right away
        if(begin(pt.obj->uuid, pt.entry->uuid, hover)) {
          t = pt;
          cur = &t;
          editMode = true;
          armHandleDrag(vs, t, vi, mouse, hover);
          Spline::buildCurve(*t.obj, *t.data, ctx.project->getAssets(), curve);
          projectSamples(curve.samples, vi, curvePx);
        }
      } else if(io.KeyCtrl) {
        clearPress(vs); // decided at release: toggles, never drags
        vs.handleArmed = true;
        vs.pressHandle = hover;
        vs.pressPos = vs.curPos = mouse;
        vs.dragGen = Editor::UndoRedo::getHistory().getGeneration();
      } else {
        if(!isPointSelected(hover))selectPoint(hover, false);
        armHandleDrag(vs, t, vi, mouse, hover);
      }
    }

    // 8. point gizmo on the selection
    bool overGizmo = false;
    if(willGizmo && !handlePressed) {
      layers.top();
      overGizmo = drawPointGizmo(layers.dl, vs, t, vi);
      layers.bottom();
    }

    // 9. press on empty image space: click (select / clear / Shift add) or box select
    if(editMode && clicked && !handlePressed && hover < 0 && !overGizmo) {
      clearPress(vs);
      vs.pressPending = true;
      vs.pressPos = vs.curPos = mouse;
    }

    // 10. while the button is down
    if(lmbDown && editMode) {
      if(vs.handleArmed && !vs.dragIdx.empty()) {
        vs.curPos = mouse;
        if(!vs.handleDragging && glm::length(mouse - vs.pressPos) > HANDLE_DRAG_PX) {
          setDragging(vs.handleDragging, true);
        }
        if(vs.handleDragging)updateHandleDrag(vs, t, vi, mouse);
      }
      if(vs.pressPending) {
        vs.curPos = mouse;
        if(!vs.boxDragging && glm::length(mouse - vs.pressPos) > BOX_DRAG_PX)vs.boxDragging = true;
      }
    }

    // 11. + 12. release (also one missed while this viewport was not drawn)
    if(!lmbDown) {
      if(editMode) {
        if(vs.handleDragging) {
          commitTarget(t, "Move Spline Points");
        } else if(vs.handleArmed) {
          // a Ctrl press (no drag armed) toggles, a plain click selects only that point
          if(vs.pressHandle >= 0 && (size_t)vs.pressHandle < t.data->points.size()) {
            selectPoint(vs.pressHandle, vs.dragIdx.empty());
          }
        } else if(vs.boxDragging) {
          boxSelect(vs, handles, size, io.KeyCtrl);
        } else if(vs.pressPending) {
          if(io.KeyShift) {
            addPointAtMouse(vs, t, vi, mouse, curve, curvePx);
          } else if(!io.KeyCtrl) {
            clearSelection(); // point edit stays active, the object selection never changes
          }
        }
      }
      clearPress(vs);
    }

    // 13. keys
    handleKeys(vs, editMode ? &t : nullptr);
    if(editMode && !session.active) { // Esc / Tab left point edit this frame: show the passive handles
      editMode = false;
      passive = vi.showSplines && resolveSelectedSpline(pt);
      if(!passive)return;
      cur = &pt;
    }

    // 6. overlay, after this frame's edits so the handles never lag behind (layer 0: below the gizmo)
    ImDrawList *dl = layers.dl;
    projectPoints(*cur, vi, handles);
    const bool showHover = canHover && cur->editable && !vs.boxDragging && !ImGuizmo::IsUsing();
    hover = showHover ? nearestHandle(handles, mouse, pickR) : -1;
    if(editMode) {
      Spline::buildCurve(*t.obj, *t.data, ctx.project->getAssets(), curve);
      projectSamples(curve.samples, vi, curvePx);
      drawCurveOverlay(dl, vi, curvePx);
    }
    drawHandles(dl, vi, *cur, handles, editMode, hover);

    if(editMode) {
      if(vs.boxDragging) {
        const glm::vec2 rMin = glm::clamp(glm::min(vs.pressPos, vs.curPos), glm::vec2{0.0f}, size);
        const glm::vec2 rMax = glm::clamp(glm::max(vs.pressPos, vs.curPos), glm::vec2{0.0f}, size);
        dl->AddRectFilled(toScreen(vi, rMin), toScreen(vi, rMax), ImGui::GetColorU32(ImGuiCol_DragDropTarget, 0.15f));
        dl->AddRect(toScreen(vi, rMin), toScreen(vi, rMax), ImGui::GetColorU32(ImGuiCol_DragDropTarget, 0.85f), 0.0f, 0, 1.5f);
      }

      const bool anyPress = vs.handleArmed || vs.pressPending || vs.boxDragging || vs.handleDragging;
      if(io.KeyShift && vi.hovered && !vi.mouseBlocked && !anyPress && hover < 0 && !overGizmo && !ImGuizmo::IsUsing()) {
        drawShiftGhost(dl, vi, vs, t, mouse, handles, curvePx);
      } else {
        vs.ghostMouse = glm::vec2{-1.0f};
      }

      // 14. banner (top layer)
      layers.top();
      drawBanner(dl, vi, t);
    }
  }

  uint32_t pickCurve(const ViewInfo &vi, glm::vec2 mouseLocal, float radiusPx)
  {
    if(!vi.showSplines || vi.overlaysHidden || !vi.view || !vi.proj)return 0;
    auto *scene = loadedScene();
    if(!scene)return 0;

    CurvePick pick{.scene = *scene, .vi = vi, .mouse = mouseLocal, .radius = radiusPx};
    pickCurveIn(scene->getRootObject(), pick);
    return pick.uuid;
  }

  bool isStaticGeometry(Project::Scene &scene, uint32_t objUUID)
  {
    auto obj = scene.getObjectByUUID(objUUID);
    if(!obj)return false;

    auto hasModel = [](const Project::Object &o) {
      return std::any_of(o.components.begin(), o.components.end(), [](const auto &c) { return c.id == 1; });
    };
    if(hasModel(*obj))return true;
    if(obj->isPrefabInstance() && ctx.project) {
      auto *src = ctx.project->getAssets().getPrefabSourceNode(*obj);
      if(src)return hasModel(*src);
    }
    return false;
  }
}
