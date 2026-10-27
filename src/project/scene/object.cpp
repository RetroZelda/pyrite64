/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "object.h"

#include "scene.h"
#include <algorithm>
#include "../../context.h"
#include "../../utils/hash.h"
#include "../../utils/jsonBuilder.h"
#include "../../utils/json.h"
#include "../../utils/logger.h"
#include "../../editor/imgui/notification.h"

using Builder = Utils::JSON::Builder;

namespace
{
  nlohmann::json serializeObj(const Project::Object &obj)
  {
    Builder builder{};
    builder.set("name", obj.name);
    builder.set("uuid", obj.uuid);

    builder.set("proportionalScale", obj.proportionalScale);
    builder.set("selectable", obj.selectable);
    builder.set("enabled", obj.enabled);

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

    nlohmann::json children = nlohmann::json::array();
    for (const auto &child : obj.children) {
      children.push_back(serializeObj(*child));
    }
    builder.set("children", children);
    return builder.doc;
  }

  bool hasCompWithId(const Project::Object &obj, int compID)
  {
    return std::any_of(obj.components.begin(), obj.components.end(), [compID](const Project::Component::Entry &comp) {
      return comp.id == compID;
    });
  }

  // Searches the tree below `node` for a prefab instance whose source node is `templateNode` and
  // that has its own `compID` component (instance-additive, written after the template's ones).
  const Project::Object* findInstanceWithComp(
    Project::AssetManager &assets, const Project::Object &node, const Project::Object &templateNode, int compID)
  {
    if (&node != &templateNode && node.isPrefabInstance() && hasCompWithId(node, compID)
      && assets.getPrefabSourceNode(node) == &templateNode)
    {
      return &node;
    }
    for (const auto &child : node.children) {
      if (auto *found = findInstanceWithComp(assets, *child, templateNode, compID))return found;
    }
    return nullptr;
  }
}

void Project::Object::addComponent(int compID) {
  if (compID < 0 || compID >= static_cast<int>(Component::TABLE.size()))return;
  auto &def = Component::TABLE[compID];

  // some components can only exist once per object (the runtime only uses the first one):
  // don't add another one and show an error message instead
  if (def.id == 11 || def.id == 14) // rigidbody, spline
  {
    // prefab instances also get the components of their source node at build time
    const Object *srcObj = nullptr;
    if (isPrefabInstance() && ctx.project) {
      srcObj = ctx.project->getAssets().getPrefabSourceNode(*this);
    }

    std::string msg{};
    if (hasCompWithId(*this, def.id) || (srcObj && srcObj != this && hasCompWithId(*srcObj, def.id)))
    {
      msg = "Object '" + name + "' already has a " + def.name + " component, cannot add another one";
    }
    else if (ctx.project)
    {
      // ... and the other way around: a prefab template node passes its components on to every
      // instance of it, an instance with its own one would end up with two. Checks the loaded scene
      // and the nested prefab references in prefab templates (the build warns about other scenes).
      auto &assets = ctx.project->getAssets();
      const Object *inst = nullptr;
      if (auto *scene = ctx.project->getScenes().getLoadedScene()) {
        inst = findInstanceWithComp(assets, scene->getRootObject(), *this, def.id);
      }
      for (const auto &entry : assets.getTypeEntries(FileType::PREFAB)) {
        if (inst)break;
        if (entry.prefab)inst = findInstanceWithComp(assets, entry.prefab->obj, *this, def.id);
      }
      if (inst) {
        msg = "Prefab instance '" + inst->name + "' already has its own " + def.name
          + " component, cannot add one to its prefab ('" + name + "')";
      }
    }

    if (!msg.empty())
    {
      Utils::Logger::log(msg, Utils::Logger::LEVEL_ERROR);
      Editor::Noti::add(Editor::Noti::Type::ERROR, msg);
      return;
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

nlohmann::json Project::Object::serialize() const {
  return serializeObj(*this);
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
    if(scene) {
      // In a scene, register in the scene's object map.
      scene->addObject(*this, childObj);
    } else {
      // In a prefab there is no scene, so keep the child tree on the object directly.
      children.push_back(childObj);
    }
  }
}
