/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "prefab.h"

#include "../../utils/json.h"
#include "../../utils/jsonBuilder.h"
#include "../../context.h"

using Builder = Utils::JSON::Builder;

std::string Project::Prefab::serialize(const Object &obj) const
{
  // createPrefabFromObject passes a SCENE object to turn into a prefab: serialize in scene
  // mode so its placed-instance children become thin references (their materialized content
  // is stripped), exactly as a freshly authored template should look.
  Builder builder{};
  builder.set(uuid);
  builder.doc["obj"] = obj.serialize(/*isTemplate*/false);
  return builder.toString();
}

std::string Project::Prefab::serialize() const
{
  // Persisting THIS prefab's template tree: never strip — it holds only authored content
  // (incl. additive override children on nested-reference nodes).
  Builder builder{};
  builder.set(uuid);
  builder.doc["obj"] = obj.serialize(/*isTemplate*/true);
  return builder.toString();
}

void Project::Prefab::deserialize(const std::string &str)
{
  auto doc = nlohmann::json::parse(str, nullptr, false);
  if(!doc.is_object())return;
  Utils::JSON::readProp(doc, uuid);
  obj.deserialize(nullptr, doc["obj"]);
}

void Project::Prefab::save(const std::string &path)
{
  Utils::FS::saveTextFile(path, serialize());
}
