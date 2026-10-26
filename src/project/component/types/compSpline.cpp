/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "../components.h"
#include "../../../context.h"
#include "../../../editor/imgui/helper.h"
#include "../../../utils/json.h"
#include "../../../utils/jsonBuilder.h"
#include "../../../utils/binaryFile.h"
#include "../../assetManager.h"
#include "../../../editor/pages/parts/viewport3D.h"
#include "../../../renderer/scene.h"
#include "../../../utils/meshGen.h"
#include "compSpline.h"
#include "../../../editor/splineEdit.h"
#include "../../../utils/logger.h"
#include "../../../editor/undoRedo.h"
#include "../../../editor/imgui/theme.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <stdexcept>

namespace SM = P64::SplineMath;

namespace
{
  constexpr float MAX_WAIT_SEC = 25.5f;       // u8 deciseconds
  constexpr int POINT_TABLE_ROWS = 10;        // visible rows of the inspector point table
  constexpr size_t LINE_VERT_BUDGET = 65528;  // shared line mesh has u16 indices, addLine() adds 8 verts

  const glm::vec4 COLOR_START{0.3f, 0.95f, 0.35f, 1.0f}; // start marker (point 0)
  const ImVec4 COLOR_ERROR{1.0f, 0.35f, 0.3f, 1.0f};
  const ImVec4 COLOR_WARN{1.0f, 0.8f, 0.25f, 1.0f};

  glm::vec3 toGlm(const SM::Vec3 &v) { return {v.x, v.y, v.z}; }
  glm::vec3 toGlm(const SM::Sample &s) { return {s.x, s.y, s.z}; }

  uint8_t secondsToDs(float sec) {
    if(!(sec > 0.0f))return 0; // also NaN (typed into an input)
    if(sec >= MAX_WAIT_SEC)return 255;
    return (uint8_t)std::lround(sec * 10.0f);
  }

  // tolerant reads for hand-edited files: anything that is not a (float range) number reads as 0
  float jsonFloat(const nlohmann::json &val) {
    if(!val.is_number())return 0.0f;
    double num = val.get<double>();
    return std::abs(num) <= (double)FLT_MAX ? (float)num : 0.0f;
  }

  uint8_t jsonWait(const nlohmann::json &val) {
    if(!val.is_number())return 0;
    double num = val.get<double>();
    if(!(num > 0.0))return 0;
    return num >= 255.0 ? 255 : (uint8_t)std::lround(num);
  }

  // Next button of a row that wraps to a new line when it no longer fits the table cell
  bool flowButton(const char* label, bool &first)
  {
    if(!first) {
      ImGui::SameLine();
      float width = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
      if(ImGui::GetContentRegionAvail().x < width)ImGui::NewLine();
    }
    first = false;
    return ImGui::Button(label);
  }
}

namespace Project::Component::Spline
{
  namespace
  {
    // Same component order Build::writeObject() uses: the prefab source node first, then the
    // components added directly on the instance.
    Found findSpline(Object &obj, AssetManager &assets, uint64_t compUUID, bool anyUUID)
    {
      if(obj.isPrefabInstance()) {
        auto *src = assets.getPrefabSourceNode(obj);
        if(src && src != &obj) {
          for(auto &comp : src->components) {
            if(comp.id == ID && (anyUUID || comp.uuid == compUUID))return {&comp, true};
          }
        }
      }
      for(auto &comp : obj.components) {
        if(comp.id == ID && (anyUUID || comp.uuid == compUUID))return {&comp, false};
      }
      return {};
    }

    // Instance overrides are keyed per object by prop NAME and belong to the components of the
    // prefab TEMPLATE (the only ones the inspector draws lock toggles for). A Spline added directly
    // on a prefab instance owns its values (the inspector edits prop.value there): overrides made
    // for a template Spline, or left over from a deleted one, must not reach it.
    // Every entry has its own Data (add, duplicate, paste and undo all create a new one).
    bool isInstanceAdditive(const Object &obj, const Data &data)
    {
      if(!obj.isPrefabInstance())return false;
      return std::any_of(obj.components.begin(), obj.components.end(), [&data](const Entry &comp) {
        return comp.id == ID && comp.data.get() == &data;
      });
    }
  }

  Settings getSettings(Object &obj, Data &data)
  {
    const bool ownValues = isInstanceAdditive(obj, data);
    auto get = [&obj, ownValues](auto &prop) { return ownValues ? prop.value : prop.resolve(obj.propOverrides); };

    Settings st{};
    st.type = get(data.splineType) == SM::TYPE_CATMULL_ROM ? SM::TYPE_CATMULL_ROM : SM::TYPE_LINEAR;
    st.closed = get(data.splineClosed); // requested, users apply SM::isClosed()
    int32_t subdiv = std::clamp(get(data.splineSubdiv), 1, (int32_t)SM::MAX_SUBDIV);
    st.subdiv = (uint8_t)SM::effectiveSubdiv(st.type, (uint32_t)subdiv);
    st.pathId = (uint16_t)std::clamp(get(data.splinePathId), 0, 0xFFFF);
    return st;
  }

  glm::vec3 WorldTransform::toLocal(const glm::vec3 &world) const
  {
    glm::vec3 s = scale;
    for(int i = 0; i < 3; ++i) {
      if(std::abs(s[i]) < 1e-4f)s[i] = std::signbit(s[i]) ? -1e-4f : 1e-4f;
    }
    return (glm::inverse(rot) * (world - pos)) / s;
  }

  WorldTransform getWorldTransform(Object &obj, AssetManager &assets)
  {
    // Must match Build::writeObject(): prefab instances write their source node's transform,
    // resolved with the INSTANCE's overrides. (The rotation is not quantized here: scene builds
    // bake world points with it, so the runtime path is exactly the one previewed.)
    Object *src = &obj;
    if(obj.isPrefabInstance()) {
      auto *node = assets.getPrefabSourceNode(obj);
      if(node)src = node;
    }

    WorldTransform xf{};
    xf.pos = src->pos.resolve(obj.propOverrides);
    xf.scale = src->scale.resolve(obj.propOverrides);
    glm::quat rot = src->rot.resolve(obj.propOverrides);
    float len = glm::length(rot);
    if(len > 1e-6f)xf.rot = rot / len; // else keep the identity default (see WorldTransform::rot)
    return xf;
  }

  void buildCurve(Object &obj, Data &data, AssetManager &assets, Curve &out)
  {
    const Settings st = getSettings(obj, data);
    const WorldTransform xf = getWorldTransform(obj, assets);
    const uint32_t count = SM::clampCount((uint32_t)std::min<size_t>(data.points.size(), UINT32_MAX));

    // world transform first, then tessellate: centripetal knots are not affine-invariant,
    // and this is the order the runtime uses for scene builds (FLAG_WORLD points)
    out.worldPts.resize(count);
    for(uint32_t i = 0; i < count; ++i) {
      glm::vec3 w = xf.toWorld(data.points[i].pos);
      out.worldPts[i] = {w.x, w.y, w.z};
    }

    out.pointCount = count;
    out.type = st.type;
    out.subdiv = st.subdiv;
    out.closed = SM::isClosed(count, st.closed);
    out.samples.resize(SM::sampleCount(count, out.closed, out.type, out.subdiv));
    uint32_t written = SM::tessellate(out.worldPts.data(), count, out.closed, out.type, out.subdiv, out.samples.data());
    out.samples.resize(written);
    out.length = written ? out.samples[written - 1].s : 0.0f;
  }

  Found findEntry(Object &obj, uint64_t compUUID, AssetManager &assets) {
    return findSpline(obj, assets, compUUID, false);
  }

  Found findFirstSpline(Object &obj, AssetManager &assets) {
    return findSpline(obj, assets, 0, true);
  }

  std::shared_ptr<void> init(Object &)
  {
    // closed smooth loop, so a new spline is visible (and editable) right away
    auto data = std::make_shared<Data>();
    data->splineType.value = SM::TYPE_CATMULL_ROM;
    data->splineClosed.value = true;
    data->splineSubdiv.value = SM::DEFAULT_SUBDIV;
    data->splinePathId.value = 0;
    data->points = {
      {{-200.0f, 0.0f, -200.0f}, 0},
      {{ 200.0f, 0.0f, -200.0f}, 0},
      {{ 200.0f, 0.0f,  200.0f}, 0},
      {{-200.0f, 0.0f,  200.0f}, 0},
    };
    return data;
  }

  nlohmann::json serialize(const Entry &entry)
  {
    const Data &data = *static_cast<const Data*>(entry.data.get());
    auto builder = Utils::JSON::Builder{}
      .set(data.splineType)
      .set(data.splineClosed)
      .set(data.splineSubdiv)
      .set(data.splinePathId);

    // float -> double is exact and the shortest round-trip form is written: reloads are bit-identical
    auto points = nlohmann::json::array();
    for(auto &p : data.points) {
      nlohmann::json jp = nlohmann::json::object();
      jp["p"] = nlohmann::json::array({p.pos.x, p.pos.y, p.pos.z});
      jp["w"] = p.waitDs;
      points.push_back(std::move(jp));
    }
    builder.doc["points"] = std::move(points);
    return builder.doc;
  }

  std::shared_ptr<void> deserialize(nlohmann::json &doc)
  {
    auto data = std::make_shared<Data>();
    if(!doc.is_object()) {
      data->splineType.value = SM::TYPE_CATMULL_ROM;
      data->splineSubdiv.value = SM::DEFAULT_SUBDIV;
      return data;
    }

    Utils::JSON::readProp(doc, data->splineType, (int32_t)SM::TYPE_CATMULL_ROM);
    Utils::JSON::readProp(doc, data->splineClosed, false);
    Utils::JSON::readProp(doc, data->splineSubdiv, (int32_t)SM::DEFAULT_SUBDIV);
    Utils::JSON::readProp(doc, data->splinePathId, (int32_t)0);

    auto itPoints = doc.find("points");
    if(itPoints == doc.end() || !itPoints->is_array())return data;

    // {"p":[x,y,z],"w":n} or a bare [x,y,z]. Points beyond MAX_POINTS are kept on purpose:
    // build() clamps and reports them, nothing is silently dropped from the file.
    for(auto &el : *itPoints) {
      const nlohmann::json *pos = &el;
      uint8_t waitDs = 0;
      if(el.is_object()) {
        auto itP = el.find("p");
        if(itP == el.end())continue;
        pos = &*itP;
        auto itW = el.find("w");
        if(itW != el.end())waitDs = jsonWait(*itW);
      }
      if(!pos->is_array() || pos->size() < 3)continue;

      data->points.push_back({
        {jsonFloat((*pos)[0]), jsonFloat((*pos)[1]), jsonFloat((*pos)[2])},
        waitDs
      });
    }
    return data;
  }

  void build(Object& obj, Entry &entry, Build::SceneCtx &ctx)
  {
    // NOTE: `ctx` is the build context here (it shadows the editor's global Context).
    // `obj` is the instance for prefab instances, overrides resolve against it.
    Data &data = getData(entry);
    const Settings st = getSettings(obj, data);

    size_t count = data.points.size();
    if(count > MAX_POINTS) {
      Utils::Logger::log("Spline '" + entry.name + "' on '" + obj.name + "' has " + std::to_string(count)
        + " points; only the first " + std::to_string(MAX_POINTS) + " are exported", Utils::Logger::LEVEL_ERROR);
      count = MAX_POINTS;
    }
    if(count < 2) {
      Utils::Logger::log("Spline '" + entry.name + "' on '" + obj.name + "' has fewer than 2 points; the runtime ignores it",
        Utils::Logger::LEVEL_WARN);
    }

    // Scene builds bake WORLD space with the exact transform writeObject() writes for this object,
    // prefab builds (no scene) keep LOCAL points: the runtime places them with the spawn transform.
    const bool bakeWorld = ctx.scene != nullptr;
    const WorldTransform xf = bakeWorld ? getWorldTransform(obj, ctx.project->getAssets()) : WorldTransform{};

    uint8_t flags = 0;
    if(st.closed)flags |= SM::FLAG_CLOSED;
    if(bakeWorld)flags |= SM::FLAG_WORLD;

    // payload v1, see splineMath.h (explicit types: write(size_t) would emit 8 bytes)
    const uint32_t start = ctx.fileObj.getPos();
    ctx.fileObj.write<uint8_t>(SM::FORMAT_VERSION);
    ctx.fileObj.write<uint8_t>(flags);
    ctx.fileObj.write<uint8_t>(st.type);
    ctx.fileObj.write<uint8_t>(st.subdiv);
    ctx.fileObj.write<uint8_t>((uint8_t)count);
    ctx.fileObj.write<uint8_t>(0); // reserved
    ctx.fileObj.write<uint16_t>(st.pathId);

    for(size_t i = 0; i < count; ++i) {
      const glm::vec3 p = bakeWorld ? xf.toWorld(data.points[i].pos) : data.points[i].pos;
      ctx.fileObj.write<float>(p.x);
      ctx.fileObj.write<float>(p.y);
      ctx.fileObj.write<float>(p.z);
    }
    for(size_t i = 0; i < count; ++i) {
      ctx.fileObj.write<uint8_t>(data.points[i].waitDs);
    }

    if(ctx.fileObj.getPos() - start != SM::payloadBytes((uint32_t)count)) {
      throw std::runtime_error("Spline payload size mismatch on '" + obj.name + "'");
    }
  }

  Utils::AABB getAABB(Object &, Entry &entry)
  {
    Data &data = getData(entry);
    Utils::AABB aabb{};
    aabb.addPoint({0.0f, 0.0f, 0.0f}); // the object origin (its icon) is always framed
    for(auto &p : data.points) {
      if(std::isfinite(p.pos.x) && std::isfinite(p.pos.y) && std::isfinite(p.pos.z))aabb.addPoint(p.pos);
    }
    // always finite, with some room around thin or single-point paths
    glm::vec3 pad = glm::max(glm::vec3{10.0f}, (aabb.max - aabb.min) * 0.05f);
    aabb.min -= pad;
    aabb.max += pad;
    return aabb;
  }

  void draw(Object &obj, Entry &entry)
  {
    if(!ctx.project)return;
    Data &data = getData(entry);
    auto &assets = ctx.project->getAssets();
    const auto &keymap = ctx.prefs.keymap;

    // `obj` may be the prefab TEMPLATE node (the inspector's edit target). The point editor, the
    // stats and world coordinates work on the selected scene object (the instance) + this entry.
    const uint32_t selUUID = ctx.selObjectUUID;
    auto scene = ctx.project->getScenes().getLoadedScene();
    auto sceneObj = scene ? scene->getObjectByUUID(selUUID) : nullptr;
    Object &instObj = sceneObj ? *sceneObj : obj;

    const bool editable = Editor::SplineEdit::canEditPoints(selUUID, entry.uuid);
    const bool editing = Editor::SplineEdit::isEditing(selUUID, entry.uuid);
    const bool templateOwned = findEntry(instObj, entry.uuid, assets).templateOwned;
    const WorldTransform xf = getWorldTransform(instObj, assets);
    const Settings st = getSettings(instObj, data);

    // copy: operations below change the selection
    const std::vector<int> selPoints = editing ? Editor::SplineEdit::selection() : std::vector<int>{};

    static Curve infoCurve{};
    buildCurve(instObj, data, assets, infoCurve);

    if(!ImTable::start("Comp", &obj))return;

    ImTable::add("Name", entry.name);

    ImTable::addObjProp<int32_t>("Type", data.splineType, [](int32_t *val) -> bool {
      const char* types[] = {"Linear", "Smooth"};
      return ImGui::Combo("##type", val, types, 2);
    }, nullptr);
    ImGui::SetItemTooltip("Linear: straight lines between the points\nSmooth: curve through every point (centripetal Catmull-Rom)");

    ImTable::addObjProp("Closed", data.splineClosed);
    ImGui::SetItemTooltip("Loop back from the last point to the first (needs 3 or more points)");

    // the type the combo above shows: addObjProp() resolves overrides only while the table is prefab-locked
    // (an instance-additive Spline is drawn unlocked and edits its own value)
    const int32_t typeShown = ImTable::isPrefabLocked() ? data.splineType.resolve(obj.propOverrides) : data.splineType.value;
    if(typeShown == SM::TYPE_CATMULL_ROM) {
      ImTable::addObjProp<int32_t>("Subdivisions", data.splineSubdiv, [](int32_t *val) -> bool {
        int32_t v = *val;
        if(!ImGui::InputInt("##subdiv", &v))return false;
        *val = std::clamp(v, 1, (int32_t)SM::MAX_SUBDIV);
        return true;
      }, nullptr);
      ImGui::SetItemTooltip("Samples per segment (1-8), the same polyline is used in-game");
    }

    ImTable::addObjProp<int32_t>("Path Id", data.splinePathId, [](int32_t *val) -> bool {
      int32_t v = *val;
      if(!ImGui::InputInt("##pathId", &v))return false;
      *val = std::clamp(v, 0, 0xFFFF);
      return true;
    }, nullptr);
    ImGui::SetItemTooltip("Guards and spawners can reference this spline by number (works inside prefabs); 0 = none");

    const uint32_t pointCount = (uint32_t)data.points.size();
    const uint32_t exportCount = SM::clampCount(pointCount);
    ImTable::add("Info");
    ImGui::Text("%u / %u points · length %.1f", (unsigned)pointCount, (unsigned)MAX_POINTS, infoCurve.length);
    ImGui::TextDisabled("record %u / %u B · N64 RAM %u B",
      (unsigned)SM::recordBytes(exportCount), (unsigned)SM::MAX_RECORD_BYTES,
      (unsigned)SM::runtimeBytes(exportCount, st.closed, st.type, st.subdiv)
    );
    if(pointCount > MAX_POINTS) {
      ImGui::TextColored(COLOR_ERROR, "Only the first %u points are exported!", (unsigned)MAX_POINTS);
    } else if(pointCount < 2) {
      ImGui::TextColored(COLOR_WARN, "Needs at least 2 points to be used in-game");
    }

    ImTable::add("Edit");
    ImGui::BeginDisabled(!editable);
    if(ImGui::Button(editing ? ICON_MDI_CHECK " Done" : ICON_MDI_VECTOR_POLYLINE_EDIT " Edit Points")) {
      if(editing) {
        Editor::SplineEdit::end();
      } else {
        Editor::SplineEdit::begin(selUUID, entry.uuid);
      }
    }
    ImGui::SetItemTooltip("%s point editing in the viewport (%s)", editing ? "Stop" : "Start", ImGui::GetKeyName(keymap.splineEditToggle));

    bool firstButton = true;
    if(flowButton(ICON_MDI_PLUS " Add", firstButton)) {
      Editor::SplineEdit::extrude(selUUID, entry.uuid);
    }
    ImGui::SetItemTooltip("Extrude a new point from the selected point or the end (%s)", ImGui::GetKeyName(keymap.splineExtrude));

    if(flowButton(ICON_MDI_TABLE_ROW_PLUS_AFTER " Insert", firstButton)) {
      Editor::SplineEdit::insertAfter(selUUID, entry.uuid, selPoints);
    }
    ImGui::SetItemTooltip("Insert a point after each selected point, or append one (%s)", ImGui::GetKeyName(keymap.splineInsertPoint));

    ImGui::BeginDisabled(selPoints.empty());
    if(flowButton(ICON_MDI_TRASH_CAN_OUTLINE " Delete", firstButton)) {
      Editor::SplineEdit::removePoints(selUUID, entry.uuid, selPoints);
    }
    ImGui::SetItemTooltip("Delete the selected points (%s)", ImGui::GetKeyName(keymap.deleteObject));
    ImGui::EndDisabled();

    if(flowButton(ICON_MDI_SWAP_HORIZONTAL " Reverse", firstButton)) {
      Editor::SplineEdit::reversePoints(selUUID, entry.uuid);
    }
    ImGui::SetItemTooltip("Reverse the point order (and with it the travel direction)");

    if(flowButton(ICON_MDI_ARROW_COLLAPSE_DOWN " Drop to Floor", firstButton)) {
      Editor::SplineEdit::dropToFloor(selUUID, entry.uuid, selPoints);
    }
    ImGui::SetItemTooltip("Move the selected points (all if none is selected) down onto the static geometry below them");
    ImGui::EndDisabled();

    // the Edit buttons above may have removed points or changed the selection / session this frame:
    // re-read both (selPoints and pointCount are from before the buttons)
    const std::vector<int> waitSel = Editor::SplineEdit::isEditing(selUUID, entry.uuid)
      ? Editor::SplineEdit::selection() : std::vector<int>{};
    if(!waitSel.empty()) {
      ImTable::add("Wait (s)");
      const int firstSel = waitSel.front();
      float waitSec = (firstSel >= 0 && firstSel < (int)data.points.size()) ? (float)data.points[firstSel].waitDs * 0.1f : 0.0f;
      ImGui::BeginDisabled(!editable);
      if(ImGui::DragFloat("##wait", &waitSec, 0.1f, 0.0f, MAX_WAIT_SEC, "%.1f", ImGuiSliderFlags_AlwaysClamp)) {
        const uint8_t waitDs = secondsToDs(waitSec);
        for(int idx : waitSel) {
          if(idx >= 0 && idx < (int)data.points.size())data.points[idx].waitDs = waitDs;
        }
      }
      if(ImGui::IsItemDeactivatedAfterEdit()) {
        Editor::SplineEdit::commit(selUUID, entry.uuid, "Set Spline Wait");
      }
      ImGui::SetItemTooltip("Patrol stop at the %d selected point(s), in seconds", (int)waitSel.size());
      ImGui::EndDisabled();
    }

    ImTable::end();

    ImGui::PushTextWrapPos(0.0f);
    if(!editable) {
      ImGui::TextDisabled("Points belong to the prefab: use 'Edit' on the prefab instance to change them (affects every instance).");
    } else if(templateOwned) {
      ImGui::TextColored(COLOR_WARN, "Editing the prefab: changes affect all instances and are not undoable.");
    }
    ImGui::PopTextWrapPos();

    static bool showWorld = false;
    ImGui::Checkbox("World", &showWorld);
    ImGui::SetItemTooltip("Show and edit the positions in world space instead of object-local space");

    if(data.points.empty()) {
      ImGui::TextDisabled("No points: press Add, or Edit Points and Shift+Click in the viewport.");
      return;
    }

    int insertAt = -1;
    int removeAt = -1;
    const int rowCount = (int)data.points.size();
    const float tableHeight = (float)(std::min(rowCount, POINT_TABLE_ROWS) + 1) * ImGui::GetFrameHeightWithSpacing() + 4_px;

    ImGui::BeginDisabled(!editable);
    if(ImGui::BeginTable("SplinePoints", 4,
      ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
      ImVec2(0.0f, tableHeight)))
    {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28_px);
      ImGui::TableSetupColumn(showWorld ? "Position (world)" : "Position (local)", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Wait (s)", ImGuiTableColumnFlags_WidthFixed, 100_px);
      ImGui::TableSetupColumn("##ops", ImGuiTableColumnFlags_WidthFixed, 52_px);
      ImGui::TableHeadersRow();

      for(int i = 0; i < rowCount; ++i)
      {
        auto &point = data.points[i];
        ImGui::PushID(i);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        char label[16];
        snprintf(label, sizeof(label), "%d", i);
        if(ImGui::Selectable(label, editing && Editor::SplineEdit::isPointSelected(i))) {
          if(editing) {
            Editor::SplineEdit::selectPoint(i, ImGui::GetIO().KeyCtrl);
          } else {
            Editor::SplineEdit::begin(selUUID, entry.uuid, i);
          }
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if(showWorld) {
          glm::vec3 posWorld = xf.toWorld(point.pos);
          if(ImGui::InputFloat3("##pos", glm::value_ptr(posWorld), "%.1f")) {
            point.pos = xf.toLocal(posWorld);
          }
        } else {
          ImGui::InputFloat3("##pos", glm::value_ptr(point.pos), "%.1f");
        }
        if(ImGui::IsItemDeactivatedAfterEdit()) {
          Editor::SplineEdit::commit(selUUID, entry.uuid, "Edit Spline Point");
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::SetNextItemWidth(-FLT_MIN);
        float waitSec = (float)point.waitDs * 0.1f;
        if(ImGui::InputFloat("##wait", &waitSec, 0.1f, 1.0f, "%.1f")) {
          point.waitDs = secondsToDs(waitSec);
        }
        if(ImGui::IsItemDeactivatedAfterEdit()) {
          Editor::SplineEdit::commit(selUUID, entry.uuid, "Set Spline Wait");
        }

        ImGui::TableSetColumnIndex(3);
        if(ImGui::SmallButton(ICON_MDI_TABLE_ROW_PLUS_AFTER))insertAt = i;
        ImGui::SetItemTooltip("Insert a point after this one");
        ImGui::SameLine();
        if(ImGui::SmallButton(ICON_MDI_TRASH_CAN_OUTLINE))removeAt = i;
        ImGui::SetItemTooltip("Delete this point");

        // highlights the handle in the viewport (hovered row is the one of the last frame)
        if(ImGui::TableGetHoveredRow() == ImGui::TableGetRowIndex()) {
          Editor::SplineEdit::setInspectorHover(i);
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    ImGui::EndDisabled();

    // deferred: both change the point list the loop above iterates
    if(insertAt >= 0)Editor::SplineEdit::insertAfter(selUUID, entry.uuid, {insertAt});
    if(removeAt >= 0)Editor::SplineEdit::removePoints(selUUID, entry.uuid, {removeAt});
  }

  // Registered as funcDrawPost3D, so the default (pickable) object icon stays. Runs after
  // ImGui::Render once per attached viewport: no ImGui calls, no markChanged, nothing cached in Data.
  void draw3D(Object& obj, Entry &entry, Editor::Viewport3D &vp, SDL_GPUCommandBuffer*, SDL_GPURenderPass*)
  {
    if(!ctx.project)return;
    Data &data = getData(entry);
    if(data.points.size() < 2)return;

    static Curve curve{};
    buildCurve(obj, data, ctx.project->getAssets(), curve);
    const uint32_t sampleCount = (uint32_t)curve.samples.size();
    if(sampleCount < 2)return;

    // selection only changes color + thickness: the line count must stay stable (VertBuffer resize)
    const bool editing = Editor::SplineEdit::isEditing(obj.uuid, entry.uuid);
    const bool selected = editing || ctx.isObjectSelected(obj.uuid);
    const float thickness = std::max(ctx.prefs.splineLineThickness, 0.0f) * (selected ? 1.6f : 1.0f);
    const glm::vec4 &color = selected ? ctx.prefs.splineLineColorSelected : ctx.prefs.splineLineColor;
    // points placed with Shift+Click sit exactly on floors, lift the lines out of the geometry
    const glm::vec3 lift{0.0f, thickness * 0.5f + 0.25f, 0.0f};

    auto &lines = *vp.getLines();
    const uint32_t segCount = SM::segmentCount(curve.pointCount, curve.closed);
    const size_t linesNeeded = (sampleCount - 1) + 2 * (size_t)segCount + 3;

    if(lines.vertLines.size() + linesNeeded * 8 > LINE_VERT_BUDGET)
    {
      // shared u16 line budget exceeded: a coarser polyline only, or nothing at all
      for(uint32_t stride = 2; stride <= 8; stride *= 2) {
        size_t pieces = (sampleCount - 1 + stride - 1) / stride;
        if(lines.vertLines.size() + pieces * 8 > LINE_VERT_BUDGET)continue;

        for(uint32_t i = 0; i + 1 < sampleCount; i += stride) {
          uint32_t next = std::min(i + stride, sampleCount - 1);
          Utils::Mesh::addLine(lines, toGlm(curve.samples[i]) + lift, toGlm(curve.samples[next]) + lift, color, thickness);
        }
        return;
      }
      return;
    }

    for(uint32_t i = 0; i + 1 < sampleCount; ++i) {
      Utils::Mesh::addLine(lines, toGlm(curve.samples[i]) + lift, toGlm(curve.samples[i + 1]) + lift, color, thickness);
    }

    // direction chevron at the arc-length middle of every control segment
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    for(uint32_t seg = 0; seg < segCount; ++seg)
    {
      float s0 = curve.samples[std::min(seg * curve.subdiv, sampleCount - 1)].s;
      float s1 = curve.samples[std::min((seg + 1) * curve.subdiv, sampleCount - 1)].s;
      float sMid = (s0 + s1) * 0.5f;

      glm::vec3 mid = toGlm(SM::positionAt(curve.samples.data(), sampleCount, curve.closed, sMid)) + lift;
      glm::vec3 dir = toGlm(SM::directionAt(curve.samples.data(), sampleCount, curve.closed, sMid));
      glm::vec3 side = glm::cross(dir, up);
      float sideLen = glm::length(side);
      side = sideLen > 1e-4f ? side / sideLen : glm::vec3{1.0f, 0.0f, 0.0f};

      float size = std::min(std::max((s1 - s0) * 0.12f, 4.0f * thickness), 30.0f * thickness);
      glm::vec3 back = mid - dir * size;
      Utils::Mesh::addLine(lines, mid, back + side * (size * 0.6f), color, thickness);
      Utils::Mesh::addLine(lines, mid, back - side * (size * 0.6f), color, thickness);
    }

    // start marker at point 0: vertical tick + small cross
    const glm::vec3 start = toGlm(curve.worldPts[0]) + lift;
    const float crossSize = 3.0f * thickness;
    Utils::Mesh::addLine(lines, start, start + glm::vec3{0.0f, 10.0f * thickness, 0.0f}, COLOR_START, thickness);
    Utils::Mesh::addLine(lines, start - glm::vec3{crossSize, 0.0f, 0.0f}, start + glm::vec3{crossSize, 0.0f, 0.0f}, COLOR_START, thickness);
    Utils::Mesh::addLine(lines, start - glm::vec3{0.0f, 0.0f, crossSize}, start + glm::vec3{0.0f, 0.0f, crossSize}, COLOR_START, thickness);
  }
}
