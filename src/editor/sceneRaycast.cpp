/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "sceneRaycast.h"

#include <cmath>
#include <functional>
#include <string>
#include "glm/geometric.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "../context.h"
#include "../project/assetManager.h"
#include "../project/scene/scene.h"
#include "../project/component/components.h"
#include "../project/component/shared/meshFilter.h"
#include "../shader/defines.h"

namespace
{
  constexpr float EPS = 1e-5f;

  using HitFn = std::function<void(float t, bool drawn, bool otherDrawn)>;

  // One mesh part, culled the way n64.frag.glsl culls it: N64Mesh::draw -> N64Material::convert hand the
  // material's drawFlags to the shader, a part without a material entry keeps flags 0 (two-sided).
  void raycastPart(const T3DM::Model &part, const Project::Assets::Model3D &model, const glm::mat4 &modelMat,
                   const Editor::SceneRaycast::Ray &ray, const float &limit, bool withCulled, const HitFn &onHit)
  {
    uint32_t drawFlags = 0;
    auto mat = model.materials.find(part.materialName);
    if(mat != model.materials.end())drawFlags = mat->second.drawFlags.value;
    const bool cullBack = (drawFlags & T3D_FLAG_CULL_BACK) != 0;
    const bool cullFront = (drawFlags & T3D_FLAG_CULL_FRONT) != 0;
    if(cullBack && cullFront)return; // never drawn

    for(auto &tri : part.triangles) {
      glm::vec3 v0(modelMat * glm::vec4(tri.vert[0].pos[0], tri.vert[0].pos[1], tri.vert[0].pos[2], 1.0f));
      glm::vec3 v1(modelMat * glm::vec4(tri.vert[1].pos[0], tri.vert[1].pos[1], tri.vert[1].pos[2], 1.0f));
      glm::vec3 v2(modelMat * glm::vec4(tri.vert[2].pos[0], tri.vert[2].pos[1], tri.vert[2].pos[2], 1.0f));

      // Möller-Trumbore ray-triangle intersection. a > 0: the counter-clockwise (front) side faces the ray,
      // as gl_FrontFacing (the pipeline's default counter-clockwise front face; the importer keeps glTF's
      // counter-clockwise outside). World space vertices, so a mirroring object transform flips it exactly
      // like the GPU's facing test.
      glm::vec3 e1 = v1 - v0, e2 = v2 - v0;
      glm::vec3 h = glm::cross(ray.dir, e2);
      float a = glm::dot(e1, h);
      if(std::abs(a) < EPS)continue;
      const bool drawn = a > 0.0f ? !cullFront : !cullBack;
      const bool otherDrawn = a > 0.0f ? !cullBack : !cullFront;
      if(!drawn && !withCulled)continue;
      float f = 1.0f / a;
      glm::vec3 s = ray.origin - v0;
      float u = f * glm::dot(s, h);
      if(u < 0.0f || u > 1.0f)continue;
      glm::vec3 q = glm::cross(s, e1);
      float v = f * glm::dot(ray.dir, q);
      if(v < 0.0f || u + v > 1.0f)continue;
      float t = f * glm::dot(e2, q);
      if(t > EPS && t < limit)onHit(t, drawn, otherDrawn);
    }
  }

  // Walks enabled objects like the viewport's iterateObjects (a disabled object hides its subtree) and reports
  // every hit with t in (EPS, limit) on what the viewport draws of their Model (Static) components. `limit` is
  // read per triangle, so onHit may shrink it (closest hit). Prefab instances use their source node's
  // components with the INSTANCE transform.
  void raycastChildren(Project::Object &parent, const Editor::SceneRaycast::Ray &ray, const float &limit,
                       bool withCulled, const HitFn &onHit)
  {
    auto &assets = ctx.project->getAssets();

    for(auto &child : parent.children)
    {
      if(!child->enabled)continue;

      auto srcObj = child.get();
      if(child->isPrefabInstance()) {
        auto *node = assets.getPrefabSourceNode(*child);
        if(node)srcObj = node;
      }

      for(auto &comp : srcObj->components)
      {
        if(comp.id != 1)continue; // 1 = Model (Static)

        auto json = Project::Component::TABLE[1].funcSerialize(comp);
        uint64_t modelUUID = json.value("model", uint64_t{0});
        if(!modelUUID)continue;

        auto *asset = assets.getEntryByUUID(modelUUID);
        if(!asset || asset->type != Project::FileType::MODEL_3D)continue;

        glm::vec3 skew{};
        glm::vec4 persp{0, 0, 0, 1};
        glm::mat4 modelMat = glm::recompose(
          child->scale.resolve(child->propOverrides),
          child->rot.resolve(child->propOverrides),
          child->pos.resolve(child->propOverrides),
          skew, persp);

        // only the parts compModel's draw3D draws: its mesh filter resolved on the drawn object. No filter (or
        // no match) gives no indices, which N64Mesh::draw takes as every part.
        Project::Component::Shared::MeshFilter filter{};
        filter.meshFilter.value = json.value("meshFilter", std::string{});
        const auto &parts = filter.filterT3DM(asset->model.t3dm.models, *child, true);
        const auto &models = asset->model.t3dm.models;
        if(parts.empty()) {
          for(auto &part : models)raycastPart(part, asset->model, modelMat, ray, limit, withCulled, onHit);
        } else {
          for(uint32_t idx : parts) {
            if(idx < models.size())raycastPart(models[idx], asset->model, modelMat, ray, limit, withCulled, onHit);
          }
        }
      }

      raycastChildren(*child, ray, limit, withCulled, onHit);
    }
  }
}

namespace Editor::SceneRaycast
{
  Ray mouseRay(const glm::vec2 &mouseLocal, const glm::vec2 &size, const glm::mat4 &view, const glm::mat4 &proj)
  {
    // same unprojection as the viewport's prefab drop (image y points down, GL window y up)
    glm::vec4 vp{0.0f, 0.0f, size.x, size.y};
    glm::vec3 screenNear{mouseLocal.x, size.y - mouseLocal.y, 0.0f};
    glm::vec3 screenFar {mouseLocal.x, size.y - mouseLocal.y, 1.0f};

    Ray ray{};
    ray.origin = glm::unProject(screenNear, view, proj, vp);
    glm::vec3 dir = glm::unProject(screenFar, view, proj, vp) - ray.origin;
    float len = glm::length(dir);
    if(len > 1e-12f)ray.dir = dir / len; // degenerate matrices keep the default direction
    return ray;
  }

  bool raycastStaticMeshes(Project::Scene &scene, const Ray &ray, glm::vec3 &outHit, float maxT, float *outT)
  {
    if(!ctx.project)return false;

    float closestT = maxT;
    bool hit = false;
    raycastChildren(scene.getRootObject(), ray, closestT, false, [&closestT, &hit](float t, bool, bool) {
      closestT = t; // only reported while closer than closestT
      hit = true;
    });
    if(!hit)return false;

    outHit = ray.origin + ray.dir * closestT;
    if(outT)*outT = closestT;
    return true;
  }

  void raycastStaticMeshesAll(Project::Scene &scene, const Ray &ray, std::vector<Hit> &outHits, float maxT)
  {
    outHits.clear();
    if(!ctx.project)return;

    const float limit = maxT;
    raycastChildren(scene.getRootObject(), ray, limit, true, [&outHits](float t, bool drawn, bool otherDrawn) {
      outHits.push_back({t, drawn, otherDrawn});
    });
  }

  bool intersectPlane(const Ray &ray, const glm::vec3 &planePoint, const glm::vec3 &planeNormal, glm::vec3 &outHit)
  {
    float denom = glm::dot(planeNormal, ray.dir);
    if(std::abs(denom) < 1e-6f)return false; // parallel to the plane

    float t = glm::dot(planePoint - ray.origin, planeNormal) / denom;
    if(!(t > 0.0f) || !std::isfinite(t))return false; // behind the ray origin

    outHit = ray.origin + ray.dir * t;
    return true;
  }

  bool projectToScreen(const glm::vec3 &world, const glm::vec2 &size, const glm::mat4 &view, const glm::mat4 &proj,
                       glm::vec2 &outPos, float *outDepth)
  {
    glm::vec4 clip = proj * (view * glm::vec4(world, 1.0f));
    if(clip.w <= 0.0f)return false; // behind the camera (perspective)

    glm::vec3 p = glm::project(world, view, proj, glm::vec4{0.0f, 0.0f, size.x, size.y});
    if(!(p.z >= 0.0f && p.z <= 1.0f))return false; // also rejects NaN

    outPos = {p.x, size.y - p.y};
    if(outDepth)*outDepth = p.z;
    return true;
  }
}
