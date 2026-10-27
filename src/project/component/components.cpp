/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "components.h"
#include "../scene/object.h"
#include "types/compSpline.h"

namespace Project::Component
{
  std::array<CompInfo, TABLE.size()> TABLE_SORTED_BY_NAME{};

  glm::mat4 makeModelMatrix(Object &obj, float vertexScale)
  {
    glm::vec3 skew{0,0,0};
    glm::vec4 persp{0,0,0,1};
    // Vertices are quantized model units, the scene works in meters, so the model's
    // vertex scale is folded into the object scale here.
    return glm::recompose(
      obj.scale.resolve(obj.propOverrides) * vertexScale,
      obj.rot.resolve(obj.propOverrides),
      obj.pos.resolve(obj.propOverrides),
      skew, persp);
  }
}

int Project::Component::remapLegacyId(int id, const nlohmann::json &data)
{
  if (!data.is_object())return id;
  if (id == 13 && data.contains("emitterUUID"))return Particle::ID;
  if (id == 14 && data.contains("points"))return Spline::ID;
  return id;
}

void Project::Component::init() {
  TABLE_SORTED_BY_NAME = TABLE;
  std::ranges::sort(TABLE_SORTED_BY_NAME, [](const CompInfo &a, const CompInfo &b) {
    return strcmp(a.name, b.name) < 0;
  });
}
