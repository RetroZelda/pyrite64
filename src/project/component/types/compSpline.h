/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>
#include <vector>
#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"
#include "../components.h"
#include "../../../utils/prop.h"
#include "engine/include/lib/splineMath.h"

namespace Project { class AssetManager; class Object; }

/**
 * Editor "Spline" component (TABLE[25]) - shared API for the component itself, the viewport point
 * editor (Editor::SplineEdit) and scene building. Points are OBJECT-LOCAL; they are shared by every
 * instance of a prefab template (a point list cannot be a prefab override).
 */
namespace Project::Component::Spline
{
  constexpr int ID = 25;                                         // == TABLE index == P64::Comp::Spline::ID
  constexpr uint32_t MAX_POINTS = P64::SplineMath::MAX_POINTS;   // 64

  struct Point
  {
    glm::vec3 pos{0.0f};   // object-local
    uint8_t waitDs{0};     // patrol stop at this point, deciseconds (0..255 = 0..25.5 s)
  };

  struct Data
  {
    PROP_S32(splineType);     // P64::SplineMath::TYPE_* (0 Linear, 1 Smooth)
    PROP_BOOL(splineClosed);
    PROP_S32(splineSubdiv);   // 1..8 samples per segment (Smooth only)
    PROP_S32(splinePathId);   // 0..65535, 0 = none; found at runtime via Comp::Spline::findByPathId
    std::vector<Point> points{};
  };

  inline Data& getData(Entry &entry) { return *static_cast<Data*>(entry.data.get()); }

  // Settings with prefab overrides resolved against `obj`, clamped exactly like build().
  // Overrides only apply to the prefab template's Spline: one added directly on a prefab instance
  // (in obj.components) always uses its own values, like the inspector shows them.
  struct Settings { uint8_t type; bool closed; uint8_t subdiv; uint16_t pathId; };
  Settings getSettings(Object &obj, Data &data);

  // The transform Build::writeObject() writes for `obj`: for prefab instances the source node's
  // pos/rot/scale resolved with obj.propOverrides. world = pos + rot * (local * scale).
  struct WorldTransform
  {
    glm::vec3 pos{0.0f};
    // identity: with GLM_FORCE_QUAT_DATA_XYZW both glm::quat{1,0,0,0} and glm::identity<glm::quat>()
    // (glm builds it as genType(1,0,0,0)) are x=1, w=0, a 180 degree turn around X
    glm::quat rot{glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f)};
    glm::vec3 scale{1.0f};
    [[nodiscard]] glm::vec3 toWorld(const glm::vec3 &local) const { return pos + rot * (local * scale); }
    [[nodiscard]] glm::vec3 toLocal(const glm::vec3 &world) const;   // |scale| floored at 1e-4 (no NaN)
  };
  WorldTransform getWorldTransform(Object &obj, AssetManager &assets);

  // World-space curve: the exact polyline the N64 builds for a scene object (points clamped to 64).
  struct Curve
  {
    std::vector<P64::SplineMath::Vec3> worldPts{};
    std::vector<P64::SplineMath::Sample> samples{};
    uint32_t pointCount{0};   // clamped control point count
    uint32_t subdiv{1};       // effective samples per segment
    uint8_t type{0};
    bool closed{false};       // effective (>= 3 points)
    float length{0.0f};
  };
  void buildCurve(Object &obj, Data &data, AssetManager &assets, Curve &out);

  // Spline entries of an object: the prefab source node's components first (templateOwned = true
  // for instances), then the object's own (instance-additive) components.
  struct Found { Entry *entry{nullptr}; bool templateOwned{false}; };
  Found findEntry(Object &obj, uint64_t compUUID, AssetManager &assets);
  Found findFirstSpline(Object &obj, AssetManager &assets);
}
