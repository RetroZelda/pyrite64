/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "components.h"
#include "types/compSpline.h"

namespace Project::Component
{
  std::array<CompInfo, TABLE.size()> TABLE_SORTED_BY_NAME{};
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
