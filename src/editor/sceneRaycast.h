/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include <vector>
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/mat4x4.hpp"

namespace Project { class Scene; }

namespace Editor::SceneRaycast
{
  struct Ray { glm::vec3 origin{0.0f}; glm::vec3 dir{0.0f, 0.0f, -1.0f}; };   // dir is unit length

  struct Hit
  {
    float t{0.0f};      // distance along the ray
    bool drawn{false};  // the viewport draws the side of the triangle the ray hits (false: that side is culled)
    bool otherDrawn{false}; // ... and the opposite side
  };

  // mouseLocal: image-local display pixels (0,0 = top-left of the viewport image); size: displayed image size.
  Ray mouseRay(const glm::vec2 &mouseLocal, const glm::vec2 &size, const glm::mat4 &view, const glm::mat4 &proj);

  // Closest hit against what the viewport draws of every Model (Static) component of enabled objects: the
  // parts its mesh filter keeps, and only the triangle sides their material does not cull (CULL_BACK /
  // CULL_FRONT draw flags, as n64.frag.glsl; counter-clockwise = front). Prefab instances use their source
  // node's components with the INSTANCE transform (exactly what the viewport draws).
  bool raycastStaticMeshes(Project::Scene &scene, const Ray &ray, glm::vec3 &outHit, float maxT = 1e20f, float *outT = nullptr);

  // Every hit with t in (0, maxT) against the same parts, unsorted, culled sides included (Hit::drawn = false):
  // for callers that need more than the closest visible surface (e.g. whether a point is inside geometry).
  void raycastStaticMeshesAll(Project::Scene &scene, const Ray &ray, std::vector<Hit> &outHits, float maxT = 1e20f);

  // Ray/plane intersection in front of the ray origin (t > 0).
  bool intersectPlane(const Ray &ray, const glm::vec3 &planePoint, const glm::vec3 &planeNormal, glm::vec3 &outHit);

  // Inverse of mouseRay: world -> image-local display pixels (y down), the viewport's box-select math.
  // False if the point is behind the camera or outside the depth range. outDepth: 0 = near .. 1 = far.
  bool projectToScreen(const glm::vec3 &world, const glm::vec2 &size, const glm::mat4 &view, const glm::mat4 &proj,
                       glm::vec2 &outPos, float *outDepth = nullptr);
}
