/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include "scene/object.h"
#include "lib/splineMath.h"

namespace P64::Comp
{
  /**
   * Authored path, created by the editor's "Spline" component (id 14).
   *
   * The editor ships the control points (world space for scene objects, object-local for prefabs,
   * which are placed with the object's transform at spawn). On load they are tessellated with
   * P64::SplineMath::tessellate - the exact code the editor preview runs - into a polyline with
   * cumulative arc length. All queries run on that polyline; distances are world units.
   *
   * The path is static after load: moving the object at runtime does not move it.
   * Only splines of ENABLED objects are live (getFirst/getNext, findByPathId), like the editor, which
   * hides disabled objects: the spline joins the live list in onEnable and leaves it in onDisable or
   * when it is destroyed. fromObject() still returns a disabled object's spline, so check
   * getOwner()->isEnabled() where that matters. getEpoch() changes whenever a spline leaves the live
   * list (disabled) or is destroyed, so callers can cache Spline* and re-resolve when it changes;
   * enabling a spline does not change it.
   */
  struct Spline
  {
    static constexpr uint32_t ID = 14;

    static uint32_t getAllocSize(void* initData);
    static void initDelete(Object& obj, Spline* data, void* initData);
    static void onEnable(Object& obj, Spline* data);   // joins the live list (repeat calls are no-ops)
    static void onDisable(Object& obj, Spline* data);  // leaves it; getEpoch() changes if it was live
    static void update(Object&, Spline*, float) {} // required: Scene::update calls it unchecked

    [[nodiscard]] bool      isValid()         const { return sampleCount >= 2 && length > 0.0f; }
    [[nodiscard]] bool      isClosed()        const { return (pathFlags & SplineMath::FLAG_CLOSED) != 0; }
    [[nodiscard]] float     getLength()       const { return length; }
    [[nodiscard]] uint16_t  getPathId()       const { return pathId; }
    [[nodiscard]] Object*   getOwner()        const { return owner; }
    [[nodiscard]] uint32_t  getPointCount()   const { return pointCount; }
    [[nodiscard]] uint32_t  getSegmentCount() const { return SplineMath::segmentCount(pointCount, isClosed()); }
    [[nodiscard]] uint32_t  getSubdiv()       const { return subdiv; }
    [[nodiscard]] uint32_t  getSampleCount()  const { return sampleCount; }
    [[nodiscard]] const SplineMath::Sample* getSamples() const {
      return reinterpret_cast<const SplineMath::Sample*>(reinterpret_cast<const char*>(this) + sizeof(Spline));
    }

    [[nodiscard]] fm_vec3_t getPoint(uint32_t idx) const;         // control point (exact), idx clamped
    [[nodiscard]] float     getPointDistance(uint32_t idx) const; // arc length at control point idx
    [[nodiscard]] float     getPointWait(uint32_t idx) const;     // seconds (0 = no stop)
    [[nodiscard]] uint32_t  getSegmentAt(float s) const;          // control segment containing s
    [[nodiscard]] float     wrapDistance(float s) const;          // closed: [0,L), open: clamp [0,L]
    [[nodiscard]] fm_vec3_t getPosition(float s) const;
    [[nodiscard]] fm_vec3_t getDirection(float s) const;          // unit, direction of increasing s

    // closest point on the whole path; returns its arc length (planarXZ ignores Y)
    float getClosest(const fm_vec3_t &p, bool planarXZ = true, float *outDistSq = nullptr) const;
    // closest point with s in [sMin, sMax] (UNWRAPPED, sMin <= sMax). Closed paths walk across the
    // seam when the window leaves [0,L]; a window inside [0,L] never wraps. Separate name on purpose.
    float getClosestInRange(const fm_vec3_t &p, float sMin, float sMax,
                            bool planarXZ = true, float *outDistSq = nullptr) const;

    [[nodiscard]] static Spline*  fromObject(const Object* obj);      // first Spline of obj (may be invalid or disabled)
    // the live (enabled) VALID spline with that id nearest to nearPos, by 3D distance: copies of one
    // path on several floors share their XZ footprint, Y picks the caller's floor
    [[nodiscard]] static Spline*  findByPathId(uint16_t wantedId, const fm_vec3_t &nearPos);
    [[nodiscard]] static Spline*  getFirst();
    [[nodiscard]] Spline*         getNext() const { return nextLive; }
    [[nodiscard]] static uint32_t getEpoch();

  private:
    Spline*  nextLive{nullptr};   // +0  intrusive live list
    Object*  owner{nullptr};      // +4
    float    length{0.0f};        // +8  total arc length
    uint16_t sampleCount{0};      // +12
    uint16_t pathId{0};           // +14
    uint8_t  pathFlags{0};        // +16 effective FLAG_CLOSED only
    uint8_t  curveType{0};        // +17
    uint8_t  subdiv{1};           // +18 effective samples per segment
    uint8_t  pointCount{0};       // +19
    uint32_t reserved{0};         // +20
    // same allocation, after the struct:
    //   SplineMath::Sample samples[sampleCount]   (16 B each, starts 4-aligned)
    //   uint8_t            waitDs[pointCount]     (deciseconds)

    [[nodiscard]] SplineMath::Sample* samplesMut() {
      return reinterpret_cast<SplineMath::Sample*>(reinterpret_cast<char*>(this) + sizeof(Spline));
    }
    [[nodiscard]] const uint8_t* waits() const {
      return reinterpret_cast<const uint8_t*>(getSamples() + sampleCount);
    }
    [[nodiscard]] uint8_t* waitsMut() {
      return reinterpret_cast<uint8_t*>(samplesMut() + sampleCount);
    }
    static void link(Spline* s);
    static bool unlink(Spline* s); // true if s was live
  };

  static_assert(sizeof(Spline) == SplineMath::RUNTIME_HEADER_BYTES, "update SplineMath::RUNTIME_HEADER_BYTES");
  static_assert(sizeof(Spline) % 8 == 0, "component data after the header must stay aligned");
}
