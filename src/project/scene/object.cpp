/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "object.h"

#include "scene.h"
#include <algorithm>
#include "../../utils/hash.h"
#include "../../utils/jsonBuilder.h"
#include "../../utils/json.h"
#include "../../utils/logger.h"
#include "../../editor/imgui/notification.h"

using Builder = Utils::JSON::Builder;

namespace
{
  nlohmann::json serializeObj(const Project::Object &obj, bool isTemplate)
  {
    Builder builder{};
    builder.set("name", obj.name);
    builder.set("uuid", obj.uuid);

    builder.set("proportionalScale", obj.proportionalScale);
    builder.set("selectable", obj.selectable);
    builder.set("enabled", obj.enabled);
    builder.set("uuidPrefabNode", obj.uuidPrefabNode);

    builder
      .set(obj.uuidPrefab)
      .set(obj.pos)
      .set(obj.rot)
      .set(obj.scale);

    auto ovr = nlohmann::json::object();
    for(auto &[key, val] : obj.propOverrides) {
      ovr[std::to_string(key)] = val.serialize();
    }
    builder.doc["propOverrides"] = ovr;

    nlohmann::json comps = nlohmann::json::array();
    for (auto &comp : obj.components) {
      auto &def = Project::Component::TABLE[comp.id];
      nlohmann::json c{};
      c["id"] = comp.id;
      c["uuid"] = comp.uuid;
      c["name"] = comp.name;
      c["data"] = def.funcSerialize(comp);
      comps.push_back(c);
    }
    builder.doc["components"] = comps;

    // Children handling depends on context:
    //  - TEMPLATE tree: every child is authored content (plain children, nested-prefab
    //    references, and ADDITIVE override children on a reference node). Reconcile never
    //    stores derived children in a template, so serialize ALL of them.
    //  - SCENE tree: children OF A PREFAB INSTANCE are reconcile-derived (recreated from the
    //    prefab templates) and must NOT be serialized; reconcile rebuilds them on load. Plain
    //    user/legacy children (uuidPrefabNode == 0) under an instance are kept. Children under
    //    a plain (non-instance) node are always kept (incl. placed prefab instances).
    nlohmann::json children = nlohmann::json::array();
    const bool objIsInstance = obj.uuidPrefab.value != 0;
    for (const auto &child : obj.children) {
      bool derived = !isTemplate && objIsInstance && child->uuidPrefabNode != 0;
      if (!derived) {
        children.push_back(serializeObj(*child, isTemplate));
      }
    }
    builder.set("children", children);
    return builder.doc;
  }
}

void Project::Object::addComponent(int compID) {
  if (compID < 0 || compID >= static_cast<int>(Component::TABLE.size()))return;
  auto &def = Component::TABLE[compID];

  // if components already contains a rigidbody don't add another one and show an error message instead
  if (def.id == 11) // rigidbody
  { 
    for (const auto &comp : components)
    {
      auto &compDef = Component::TABLE[comp.id];
      
      if (compDef.id == 11)
      {
        Utils::Logger::log("Object '" + name + "' already has a Rigidbody component, cannot add another one", Utils::Logger::LEVEL_ERROR);
        Editor::Noti::add(Editor::Noti::Type::ERROR, "Object '" + name + "' already has a Rigidbody component, cannot add another one");
        return;
      }
    }
  }

  components.push_back({
    .id = compID,
    .uuid = Utils::Hash::sha256_64bit(
      std::to_string(rand()) + std::to_string(compID)
    ),
    .name = std::string{def.name},
    .data = def.funcInit(*this)
  });
}

void Project::Object::removeComponent(uint64_t uuid) {
  std::erase_if(
    components,
    [uuid](const Component::Entry &entry) {
      return entry.uuid == uuid;
    }
  );
}

nlohmann::json Project::Object::serialize(bool isTemplate) const {
  return serializeObj(*this, isTemplate);
}

void Project::Object::deserialize(Scene *scene, nlohmann::json &doc)
{
  if(!doc.is_object())return;

  // Note: a legacy "id" field may be present in older scenes; it is intentionally
  // ignored. Runtime ids are assigned during build, never loaded from disk.
  name = doc["name"];
  uuid = doc["uuid"];

  proportionalScale = doc.value("proportionalScale", false);
  selectable = doc.value("selectable", true);
  enabled = doc.value("enabled", true);
  uuidPrefabNode = doc.value("uuidPrefabNode", 0u);

  Utils::JSON::readProp(doc, uuidPrefab);
  Utils::JSON::readProp(doc, pos);
  Utils::JSON::readProp(doc, rot);
  Utils::JSON::readProp(doc, scale, {1,1,1});

  propOverrides.clear();
  if(doc.contains("propOverrides"))
  {
    auto &overrides = doc["propOverrides"];
    for (auto& [key, val] : overrides.items())
    {
      uint64_t keyInt = std::stoull(std::string(key));
      GenericValue v{};
      v.deserialize(val);
      propOverrides[keyInt] = v;
    }
  }

  if(doc.contains("components")) {
    auto &cmArray = doc["components"];
    int count = cmArray.size();
    for (int i=0; i<count; ++i) {
      auto compObj = cmArray.at(i);

      auto id = compObj["id"];
      if (id < 0 || id >= static_cast<int>(Component::TABLE.size()))continue;
      auto &def = Component::TABLE[id];

      components.push_back({
        .id = id,
        .uuid = compObj["uuid"],
        .name = compObj["name"],
        .data = def.funcDeserialize(compObj["data"])
      });

    }
  }

  if(!doc.contains("children"))return;

  auto &chArray = doc["children"];
  size_t childCount = chArray.size();

  for (size_t i=0; i<childCount; ++i) {
    auto childObj = std::make_shared<Object>(*this);
    childObj->deserialize(scene, chArray[i]);
    // With a scene, register the child in the scene's object map. Without one
    // (e.g. loading a prefab template) just attach it to the parent so the
    // full subobject hierarchy is retained.
    if(scene) {
      scene->addObject(*this, childObj);
    } else {
      children.push_back(childObj);
    }
  }
}
