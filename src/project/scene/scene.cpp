/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "scene.h"
#include "object.h"
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <filesystem>
#include "../../utils/json.h"
#include "../../context.h"
#include "../../utils/hash.h"
#include "../../utils/jsonBuilder.h"
#include "../../utils/logger.h"

#define __LIBDRAGON_N64SYS_H 1
#define PhysicalAddr(a) (uint64_t)(a)
#include "../graph/nodes/nodeObjDel.h"
#include "include/rdpq_macros.h"
#include "include/rdpq_mode.h"

namespace
{
  constexpr float DEF_MODEL_SCALE = 1.0f;

  /**
   * Checks whether a target object belongs to the subtree of a given ancestor.
   *
   * @param allegedDescendant Object to check if is a descendant.
   * @param allegedAncestorUUID UUID of the node that may be an ancestor of target.
   * @return True when target is the ancestor itself or one of its descendants.
   */
  bool isDescendantOf(const Project::Object* allegedDescendant, uint32_t allegedAncestorUUID)
  {
    const Project::Object* current = allegedDescendant;

    // Walk from the target up to the root looking for the ancestor UUID
    while (current) {
      // Found the ancestor in the parent chain
      if (current->uuid == allegedAncestorUUID)
        return true;
      // Jump up to the parent
      current = current->parent;
    }
    // Reached the root without finding the ancestor
    return false;
  }
}

nlohmann::json Project::SceneConf::serialize() const {

  auto writeLayer = [](Utils::JSON::Builder &b, const LayerConf &layer) {
    b.set(layer.name);
    b.set(layer.depthCompare);
    b.set(layer.depthWrite);
    b.set(layer.blender);
    b.set(layer.fog);
    b.set(layer.fogColorMode);
    b.set(layer.fogColor);
    b.set(layer.fogMin);
    b.set(layer.fogMax);
    b.set(layer.lightMode);
  };

  Utils::JSON::Builder builder{};
  builder.set(name)
    .set("fbWidth", fbWidth)
    .set("fbHeight", fbHeight)
    .set("fbFormat", fbFormat)
    .set(clearColor)
    .set(doClearColor)
    .set(doClearDepth)
    .set(renderPipeline)
    .set(frameLimit)
    .set(filter)
    .set(audioFreq)
    .set(physicsTickRate)
    .set(gravity)
    .set(visualUnitsPerMeter)
    .set(velocitySolverIterations)
    .set(positionSolverIterations)
    .set(interpolatePhysicsTransforms)
    .setArray<LayerConf>("layers3D", layers3D, writeLayer)
    .setArray<LayerConf>("layersPtx", layersPtx, writeLayer)
    .setArray<LayerConf>("layers2D", layers2D, writeLayer);

  return builder.doc;
}

Project::Scene::Scene(int id_, const std::string &projectPath)
  : id{id_}
{
  Utils::Logger::log("Loading scene: " + std::to_string(id));
  scenePath = projectPath + "/data/scenes/" + std::to_string(id);

  deserialize(Utils::FS::loadTextFile(scenePath + "/scene.json"));

  root.runtimeId = 0;
  root.name = "Scene";
  root.uuid = Utils::Hash::sha256_64bit(root.name);
}

std::shared_ptr<Project::Object> Project::Scene::addObject(std::string &objJson, uint64_t parentUUID)
{
  auto p = getObjectByUUID(parentUUID);
  Object *parent = p ? p.get() : &root;

  auto json = nlohmann::json::parse(objJson, nullptr, false);
  auto obj = std::make_shared<Object>(*parent);
  obj->deserialize(this, json);
  return addObject(*parent, obj, true);
}

std::shared_ptr<Project::Object> Project::Scene::addObject(Object &parent) {
  auto child = std::make_shared<Object>(parent);
  child->name = "New Object";
  child->scale.value = {DEF_MODEL_SCALE, DEF_MODEL_SCALE, DEF_MODEL_SCALE};
  child->rot.value = {0,0,0,1};
  return addObject(parent, child, true);
}

std::shared_ptr<Project::Object> Project::Scene::addObject(Object&parent, std::shared_ptr<Object> obj, bool generateUUID) {
  parent.children.push_back(obj);

  auto setChildUUIDs = [this, generateUUID](const std::shared_ptr<Object> &objChild, auto& setChildUIDsRef) -> void
  {
    if(generateUUID)
    {
      objChild->uuid = Utils::Hash::randomU64();
    }

    objectsMap[objChild->uuid] = objChild;
    for(const auto& child : objChild->children) {
      setChildUIDsRef(child, setChildUIDsRef);
    }
  };

  setChildUUIDs(obj, setChildUUIDs);
  return obj;
}

std::shared_ptr<Project::Object> Project::Scene::addPrefabInstance(uint64_t prefabUUID)
{
  auto prefab = ctx.project->getAssets().getPrefabByUUID(prefabUUID);
  if (!prefab)return nullptr;

  auto obj = std::make_shared<Object>(root);
  obj->name += prefab->obj.name;
  obj->uuid = Utils::Hash::randomU32();
  obj->pos = prefab->obj.pos;
  obj->rot = prefab->obj.rot;
  obj->scale = prefab->obj.scale;

  obj->uuidPrefab.value = prefab->uuid.value; // Link to prefab
  obj->uuidPrefabNode = prefab->obj.uuid;     // Mirror the prefab root node
  obj->addPropOverride(obj->pos); // by default allow transforming the instance
  obj->addPropOverride(obj->rot);
  obj->addPropOverride(obj->scale);

  auto result = addObject(root, obj);
  syncInstanceChildren(*obj, prefab->uuid.value, prefab->obj, 1);
  return result;
}

std::shared_ptr<Project::Object> Project::Scene::addPrefabInstance(uint64_t prefabUUID, Object& parent)
{
  auto prefab = ctx.project->getAssets().getPrefabByUUID(prefabUUID);
  if (!prefab) return nullptr;

  auto obj = std::make_shared<Object>(parent);
  obj->name = prefab->obj.name;
  obj->uuid = Utils::Hash::randomU32();
  obj->pos.value = parent.pos.resolve(parent.propOverrides);
  obj->rot = prefab->obj.rot;
  obj->scale = prefab->obj.scale;

  obj->uuidPrefab.value = prefab->uuid.value;
  obj->uuidPrefabNode = prefab->obj.uuid; // Mirror the prefab root node
  obj->addPropOverride(obj->pos);
  obj->addPropOverride(obj->rot);
  obj->addPropOverride(obj->scale);

  auto result = addObject(parent, obj);
  syncInstanceChildren(*obj, prefab->uuid.value, prefab->obj, 1);
  return result;
}

namespace
{
  // Guard against runaway recursion from cyclic/nested prefab references.
  constexpr int MAX_PREFAB_DEPTH = 32;

  // Copies the display/transform data from a prefab template node onto an instance node.
  // Component data is intentionally NOT copied: it resolves live from the prefab.
  void copyPrefabNodeData(Project::Object &dst, const Project::Object &src)
  {
    dst.name = src.name;
    dst.pos = src.pos;
    dst.rot = src.rot;
    dst.scale = src.scale;
    dst.proportionalScale = src.proportionalScale;
    dst.enabled = src.enabled;
    dst.selectable = src.selectable;
  }

  // Initializes an instance node from a template node within ownerPrefabUUID, resolving any
  // nested prefab references. Stores the owner+slot identity (for reconcile matching), the
  // resolved node's data, and the merged overrides. Returns the resolved template so the
  // caller can materialize the correct (possibly inner-prefab) children.
  Project::PrefabResolve applyPrefabTemplate(
    Project::Object &inst, uint64_t ownerPrefabUUID, Project::Object &templateNode)
  {
    auto resolved = ctx.project->getAssets().resolveTemplateNode(templateNode, ownerPrefabUUID);
    Project::Object *dataNode = resolved.node ? resolved.node : &templateNode;

    inst.uuidPrefab.value = ownerPrefabUUID;  // owner prefab (reconcile match)
    inst.uuidPrefabNode = templateNode.uuid;  // slot within the owner prefab
    copyPrefabNodeData(inst, *dataNode);       // base data from the deepest resolved node
    inst.name = templateNode.name;             // prefer the slot node's authored name
    inst.propOverrides = resolved.overrides;   // overrides authored along the chain
    return resolved;
  }
}

void Project::Scene::unregisterObjectTree(const Object &obj)
{
  for (const auto &child : obj.children) {
    unregisterObjectTree(*child);
  }
  objectsMap.erase(obj.uuid);
}

std::shared_ptr<Project::Object> Project::Scene::materializeInstanceNode(
  Object &parent, uint64_t ownerPrefabUUID, Object &templateNode, int depth)
{
  auto inst = std::make_shared<Object>(parent);
  inst->uuid = Utils::Hash::randomU32();
  applyPrefabTemplate(*inst, ownerPrefabUUID, templateNode);

  parent.children.push_back(inst);
  objectsMap[inst->uuid] = inst;

  if (depth < MAX_PREFAB_DEPTH) {
    syncInstanceChildren(*inst, ownerPrefabUUID, templateNode, depth);
  }
  return inst;
}

void Project::Scene::syncInstanceChildren(
  Object &inst, uint64_t ownerPrefabUUID, Object &templateNode, int depth)
{
  if (depth >= MAX_PREFAB_DEPTH) return;

  auto resolved = ctx.project->getAssets().resolveTemplateNode(templateNode, ownerPrefabUUID);
  Object *dataNode = resolved.node ? resolved.node : &templateNode;

  // 1) Derived content: children of the resolved (inner) prefab node, owned by that prefab.
  syncChildSet(inst, resolved.prefabUUID, dataNode->children, depth);

  // 2) Additive overrides: when templateNode is a nested reference (it resolved to a deeper
  //    node), its OWN stored children are additions made in the OWNER prefab's copy of this
  //    instance. They are owned by ownerPrefabUUID and coexist with the derived set.
  //    The resolved.prefabUUID != ownerPrefabUUID guard prevents a cyclic/self-referential
  //    prefab from running BOTH passes with the same owner (which would make them delete each
  //    other's children every frame).
  if (dataNode != &templateNode && resolved.prefabUUID != ownerPrefabUUID) {
    syncChildSet(inst, ownerPrefabUUID, templateNode.children, depth);
  }
}

void Project::Scene::syncChildSet(
  Object &inst, uint64_t setOwnerPrefabUUID,
  const std::vector<std::shared_ptr<Object>> &templateChildren, int depth)
{
  // Remove managed children of THIS owner whose template node no longer exists.
  std::erase_if(inst.children, [&](const std::shared_ptr<Object> &child) {
    bool managed = child->uuidPrefab.value == setOwnerPrefabUUID && child->uuidPrefabNode != 0;
    if (!managed) return false; // different owner / user-added -> keep

    bool stillExists = std::any_of(
      templateChildren.begin(), templateChildren.end(),
      [&](const std::shared_ptr<Object> &sc) { return sc->uuid == child->uuidPrefabNode; }
    );
    if (!stillExists) {
      unregisterObjectTree(*child);
      return true;
    }
    return false;
  });

  // Add missing template children; refresh + recurse existing ones.
  for (const auto &sc : templateChildren) {
    auto it = std::find_if(
      inst.children.begin(), inst.children.end(),
      [&](const std::shared_ptr<Object> &child) {
        return child->uuidPrefab.value == setOwnerPrefabUUID && child->uuidPrefabNode == sc->uuid;
      }
    );

    if (it == inst.children.end()) {
      materializeInstanceNode(inst, setOwnerPrefabUUID, *sc, depth + 1);
    } else {
      applyPrefabTemplate(**it, setOwnerPrefabUUID, *sc);
      syncInstanceChildren(**it, setOwnerPrefabUUID, *sc, depth + 1);
    }
  }
}

void Project::Scene::reconcilePrefabInstances()
{
  auto &assets = ctx.project->getAssets();

  std::function<void(Object&)> visit = [&](Object &obj) {
    // A "top-level" instance is a prefab instance whose parent is NOT a prefab instance.
    // syncInstanceChildren recursively syncs its ENTIRE managed subtree (derived + additive)
    // in one pass with a monotonic depth budget; nested instances are handled there, so we do
    // NOT re-enter visit() into a top-level instance's children (avoids resetting the depth
    // budget per level, which would grow without bound for a cyclic prefab).
    const bool topLevelInstance = obj.isPrefabInstance()
      && !(obj.parent && obj.parent->isPrefabInstance());

    if (topLevelInstance) {
      auto *slot = assets.getPrefabSlotNode(obj);
      if (slot) {
        syncInstanceChildren(obj, obj.uuidPrefab.value, *slot, 1);
      }
    } else if (!obj.isPrefabInstance()) {
      for (auto &child : obj.children) {
        visit(*child);
      }
    }
  };

  for (auto &child : root.children) {
    visit(*child);
  }
}

void Project::Scene::removeObjectAndChildren(Object &obj)
{
  ctx.removeObjectSelection(obj.uuid);
  unregisterObjectTree(obj);
  if(obj.parent) {
    std::erase_if(
      obj.parent->children,
      [&obj](const std::shared_ptr<Object> &ref) { return ref->uuid == obj.uuid; }
    );
  }
}

bool Project::Scene::isPrefabSubobject(const Object &o) const
{
  // Any node whose parent is a prefab instance is a managed/derived subobject (users
  // cannot add plain children to instances). This holds across nested prefab boundaries.
  return o.isPrefabInstance() && o.parent && o.parent->isPrefabInstance();
}

bool Project::Scene::isPrefabInstanceRoot(const Object &o) const
{
  if(!o.isPrefabInstance()) return false;
  // Placed scene instance: its parent is not a prefab instance.
  if(!(o.parent && o.parent->isPrefabInstance())) return true;
  // Nested reference: the node's slot in its OWNER prefab is itself a prefab reference.
  // (A plain materialized subobject's slot is a plain node -> not an instance root.)
  auto *slot = ctx.project->getAssets().getPrefabSlotNode(o);
  return slot && slot->isPrefabInstance();
}

bool Project::Scene::isEditingPrefabOf(const Object &o) const
{
  if(!o.isPrefabInstance()) return false;

  // Climb to the topmost prefab-instance ancestor (handles nested instances).
  const Object *instRoot = &o;
  while(instRoot->parent && instRoot->parent->isPrefabInstance()) {
    instRoot = instRoot->parent;
  }

  // Edit mode is active if any node of this instance tree has isPrefabEdit set.
  std::function<bool(const Object&)> anyEditing = [&](const Object &n) -> bool {
    if(n.isPrefabEdit) return true;
    for(const auto &c : n.children) {
      if(c->isPrefabInstance() && anyEditing(*c)) return true;
    }
    return false;
  };
  return anyEditing(*instRoot);
}

Project::Object* Project::Scene::instanceRootForPrefab(Object &node, uint64_t prefabUUID)
{
  auto &assets = ctx.project->getAssets();
  Object *cur = &node;
  while(cur) {
    if(cur->isPrefabInstance()) {
      // An instance ROOT is either the placed scene instance (parent is not a prefab instance)
      // or a nested-reference node (its slot in its owner prefab is itself a prefab reference).
      bool placedRoot = !(cur->parent && cur->parent->isPrefabInstance());
      Object *slot = assets.getPrefabSlotNode(*cur);
      bool nestedRef = slot && slot->isPrefabInstance();
      if((placedRoot || nestedRef) && assets.resolveInstance(*cur).prefabUUID == prefabUUID) {
        return cur;
      }
    }
    cur = cur->parent;
  }
  return nullptr;
}

Project::Scene::AddTarget Project::Scene::resolveAddTarget(Object &node)
{
  auto &assets = ctx.project->getAssets();
  if(!node.isPrefabInstance()) return {}; // plain scene object: caller does a normal scene add

  Object *slot = assets.getPrefabSlotNode(node);
  const bool isInstanceRoot = slot && slot->isPrefabInstance();
  const uint64_t ownerPrefab = node.uuidPrefab.value;

  // Case 1: node is a nested-instance root and ITS OWN prefab is in edit mode -> edit that
  // prefab's own definition (affects every instance of it).
  if(isInstanceRoot && node.isPrefabEdit) {
    auto r = assets.resolveInstance(node);
    if(r.node) return { r.node, r.prefabUUID, false };
  }

  // Case 2: node's OWNER prefab is in edit mode -> add to node's slot in the owner template.
  // For an instance root this is an ADDITIVE override on the owner's copy of node; for a plain
  // subobject it is simply editing the owner prefab's own content.
  if(ownerPrefab != 0 && slot) {
    Object *ownerRoot = instanceRootForPrefab(node, ownerPrefab);
    if(ownerRoot && ownerRoot->isPrefabEdit) {
      return { slot, ownerPrefab, isInstanceRoot };
    }
  }

  return {}; // not in an edit scope that allows adding here
}

Project::Scene::AddTarget Project::Scene::resolveEditTarget(Object &node)
{
  auto &assets = ctx.project->getAssets();
  if(!node.isPrefabInstance()) return {};

  Object *slot = assets.getPrefabSlotNode(node);
  const bool isInstanceRoot = slot && slot->isPrefabInstance();

  // Case 1: node's OWN prefab is in edit mode -> edit that prefab's definition directly.
  if(isInstanceRoot && node.isPrefabEdit) {
    auto r = assets.resolveInstance(node);
    if(r.node) return { r.node, r.prefabUUID, false };
  }

  // Case 2: node's OWNER prefab is in edit mode -> edit/override node's slot in the owner.
  if(slot) {
    Object *ownerRoot = instanceRootForPrefab(node, node.uuidPrefab.value);
    if(ownerRoot && ownerRoot->isPrefabEdit) {
      return { slot, node.uuidPrefab.value, isInstanceRoot };
    }
  }

  return {};
}

std::shared_ptr<Project::Object> Project::Scene::addPrefabReferenceToPrefab(
  Object &destInstNode, uint64_t innerPrefabUUID)
{
  auto &assets = ctx.project->getAssets();
  auto target = resolveAddTarget(destInstNode);
  if(!target.parent) return nullptr;

  // Reject cycles: the prefab being modified must not be (transitively) the embedded prefab.
  if(assets.prefabContains(innerPrefabUUID, target.prefabUUID)) return nullptr;

  auto innerPrefab = assets.getPrefabByUUID(innerPrefabUUID);
  if(!innerPrefab) return nullptr;

  auto node = std::make_shared<Object>(*target.parent);
  node->name = innerPrefab->obj.name;
  node->uuid = Utils::Hash::randomU32();
  node->uuidPrefab.value = innerPrefabUUID;
  node->uuidPrefabNode = innerPrefab->obj.uuid;
  node->pos = innerPrefab->obj.pos;
  node->rot = innerPrefab->obj.rot;
  node->scale = innerPrefab->obj.scale;
  node->addPropOverride(node->pos);
  node->addPropOverride(node->rot);
  node->addPropOverride(node->scale);
  target.parent->children.push_back(node);

  assets.markPrefabDirty(target.prefabUUID);
  reconcilePrefabInstances();

  for(const auto &c : destInstNode.children) {
    if(c->uuidPrefab.value == target.prefabUUID && c->uuidPrefabNode == node->uuid) {
      return c;
    }
  }
  return nullptr;
}

std::shared_ptr<Project::Object> Project::Scene::prefabAddChild(Object &instNode)
{
  // Route to the innermost prefab in edit mode (or an additive override on an outer prefab).
  auto target = resolveAddTarget(instNode);
  if(!target.parent) return nullptr;

  auto child = std::make_shared<Object>(*target.parent);
  child->name = "New Object";
  child->uuid = Utils::Hash::randomU32();
  child->scale.value = {DEF_MODEL_SCALE, DEF_MODEL_SCALE, DEF_MODEL_SCALE};
  child->rot.value = {0, 0, 0, 1};
  target.parent->children.push_back(child);

  ctx.project->getAssets().markPrefabDirty(target.prefabUUID);
  reconcilePrefabInstances();

  // Return the freshly materialized instance child mirroring the new prefab node.
  for(const auto &c : instNode.children) {
    if(c->uuidPrefab.value == target.prefabUUID && c->uuidPrefabNode == child->uuid) {
      return c;
    }
  }
  return nullptr;
}

void Project::Scene::prefabRemoveNode(Object &instSubobj)
{
  // A node lives as a stored child of its OWNER prefab (a derived B-subobject in B; an additive
  // override in A). Removing it edits that owner prefab, so the owner must be in edit mode.
  uint64_t prefabUUID = instSubobj.uuidPrefab.value;
  Object *ownerRoot = instanceRootForPrefab(instSubobj, prefabUUID);
  if(!ownerRoot || !ownerRoot->isPrefabEdit) return; // owner prefab not in edit mode -> locked

  auto *node = ctx.project->getAssets().getPrefabSlotNode(instSubobj);
  if(!node || !node->parent) return; // never remove the prefab root

  std::erase_if(
    node->parent->children,
    [node](const std::shared_ptr<Object> &c) { return c.get() == node; }
  );
  ctx.project->getAssets().markPrefabDirty(prefabUUID);
  reconcilePrefabInstances();
}

int Project::Scene::prefabMove(Object *src, Object *tgt, bool asChild)
{
  if(!src) return 0;

  auto &assets = ctx.project->getAssets();

  // Destination node in instance space (where the dragged item should land).
  Object *dest = asChild ? tgt : (tgt ? tgt->parent : nullptr);

  // Dropping anything into a prefab instance subtree is only allowed while editing it.
  if(dest && dest->isPrefabInstance() && !isEditingPrefabOf(*dest)) {
    return 2;
  }

  // --- Source is a prefab subobject -------------------------------------------------
  if(isPrefabSubobject(*src)) {
    uint64_t ownerPrefab = src->uuidPrefab.value;
    // Per-level: moving/removing a node edits its OWNER prefab, so the owner must be in edit
    // mode (a derived B-subobject needs B edited; an additive override needs A edited).
    Object *srcOwnerRoot = instanceRootForPrefab(*src, ownerPrefab);
    if(!srcOwnerRoot || !srcOwnerRoot->isPrefabEdit) return 2;

    auto *srcSlot = assets.getPrefabSlotNode(*src);
    if(!srcSlot || !srcSlot->parent) return 2;

    // Reparent within the same owner prefab (destination must be a plain node of it).
    if(dest && dest->isPrefabInstance() && dest->uuidPrefab.value == ownerPrefab
       && isEditingPrefabOf(*dest)) {
      auto *destSlot = assets.getPrefabSlotNode(*dest);
      // Cannot nest under a reference node (its children are derived, not stored).
      if(!destSlot || destSlot->isPrefabInstance()) return 2;

      // Disallow moving a node into its own descendant.
      for(Object *p = destSlot; p; p = p->parent) {
        if(p == srcSlot) return 2;
      }

      std::shared_ptr<Object> held;
      auto &oldSiblings = srcSlot->parent->children;
      auto it = std::find_if(oldSiblings.begin(), oldSiblings.end(),
        [srcSlot](const std::shared_ptr<Object> &c) { return c.get() == srcSlot; });
      if(it != oldSiblings.end()) { held = *it; oldSiblings.erase(it); }
      if(held) {
        held->parent = destSlot;
        destSlot->children.push_back(held);
      }
      assets.markPrefabDirty(ownerPrefab);
      reconcilePrefabInstances();
      return 1;
    }

    // Otherwise: drag OUT of the prefab -> standalone scene object. The slot is a TEMPLATE
    // node, so serialize in template mode to preserve its authored/additive content.
    auto json = srcSlot->serialize(/*isTemplate*/true);
    std::erase_if(srcSlot->parent->children,
      [srcSlot](const std::shared_ptr<Object> &c) { return c.get() == srcSlot; });
    assets.markPrefabDirty(ownerPrefab);

    Object *standaloneParent = &root;
    if(dest && dest != &root && !dest->isPrefabInstance()) standaloneParent = dest;

    auto newObj = std::make_shared<Object>(*standaloneParent);
    newObj->deserialize(nullptr, json);
    addObject(*standaloneParent, newObj, true);

    reconcilePrefabInstances();
    return 1;
  }

  // --- Source dragged INTO an edited prefab (plain object, or another prefab instance
  //     which becomes a nested prefab reference) --------------------------------------
  if(dest && dest->isPrefabInstance() && isEditingPrefabOf(*dest)) {
    // Disallow dragging an object into its own subtree.
    if(isDescendantOf(dest, src->uuid)) return 2;

    // Per-level routing: into the innermost edited prefab's definition, or as an additive
    // override on the owner prefab's copy of dest (when only an outer prefab is edited).
    auto target = resolveAddTarget(*dest);
    if(!target.parent) return 2;

    // Reject moves that would create a cyclic prefab reference.
    if(src->isPrefabInstance() && assets.prefabContains(src->uuidPrefab.value, target.prefabUUID)) {
      return 2;
    }

    auto json = src->serialize();
    auto newNode = std::make_shared<Object>(*target.parent);
    newNode->deserialize(nullptr, json);
    target.parent->children.push_back(newNode);

    removeObjectAndChildren(*src);
    assets.markPrefabDirty(target.prefabUUID);
    reconcilePrefabInstances();
    return 1;
  }

  return 0; // not a prefab-aware move
}

void Project::Scene::commitInstanceSubtreeToPrefab(Object &node)
{
  auto &assets = ctx.project->getAssets();
  std::unordered_set<uint64_t> dirty;

  std::function<void(Object&)> walk = [&](Object &n) {
    if (n.isPrefabInstance()) {
      // Per-level: write to the node's own prefab definition if that prefab is in edit mode,
      // else to an additive override on the owner prefab's copy (resolveEditTarget).
      auto t = resolveEditTarget(n);
      if (t.parent) {
        glm::vec3 p = n.pos.resolve(n.propOverrides);
        glm::quat r = n.rot.resolve(n.propOverrides);
        glm::vec3 s = n.scale.resolve(n.propOverrides);

        // A plain template node stores the transform directly; a nested-reference node
        // stores it as a placement override (its base comes from the inner prefab).
        if (t.parent->isPrefabInstance()) {
          t.parent->addPropOverride(t.parent->pos);
          t.parent->addPropOverride(t.parent->rot);
          t.parent->addPropOverride(t.parent->scale);
        }
        t.parent->pos.resolve(t.parent->propOverrides) = p;
        t.parent->rot.resolve(t.parent->propOverrides) = r;
        t.parent->scale.resolve(t.parent->propOverrides) = s;
        dirty.insert(t.prefabUUID);
      }
    }
    for (auto &c : n.children) walk(*c);
  };
  walk(node);

  for (uint64_t uuid : dirty) assets.markPrefabDirty(uuid);
}

void Project::Scene::removeObject(Object &obj) {
  ctx.removeObjectSelection(obj.uuid);

  std::erase_if(
    obj.parent->children,
    [&obj](const std::shared_ptr<Object> &ref) { return ref->uuid == obj.uuid; }
  );
  objectsMap.erase(obj.uuid);
}

void Project::Scene::removeAllObjects() {
  objectsMap.clear();
  root.children.clear();
}

bool Project::Scene::moveObject(uint32_t uuidObject, uint32_t uuidTarget, bool asChild)
{
  if(uuidObject == uuidTarget) {
    return false;
  }

  auto objIt = objectsMap.find(uuidObject);
  auto targetIt = objectsMap.find(uuidTarget);
  bool targetIsRoot = uuidTarget == root.uuid;
  if (objIt == objectsMap.end() || (!targetIsRoot && targetIt == objectsMap.end())) {
    return false;
  }

  auto obj = objIt->second;
  auto target = targetIsRoot ? std::shared_ptr<Object>{} : targetIt->second;

  // Moving object into descendant --> Disallow
  if (!targetIsRoot && isDescendantOf(target.get(), uuidObject))
    return false;

  // Remove from current parent
  if (obj->parent) {
    std::erase_if(
      obj->parent->children,
      [&obj](const std::shared_ptr<Object> &ref) { return ref->uuid == obj->uuid; }
    );
  }

  if (asChild) {
    // Add as child to target (or root)
    if (targetIsRoot) {
      root.children.push_back(obj);
      obj->parent = &root;
    } else {
      target->children.push_back(obj);
      obj->parent = target.get();
    }
  } else {
    // Special case: insert at top if dropping above root
    if (uuidTarget == root.uuid) {
      root.children.insert(root.children.begin(), obj);
      obj->parent = &root;
    } else {
      // Add as sibling to target
      auto parent = target->parent;
      if (parent) {
        // insert after target
        auto &siblings = parent->children;
        auto it = std::find_if(
          siblings.begin(), siblings.end(),
          [&target](const std::shared_ptr<Object> &ref) { return ref->uuid == target->uuid; }
        );
        if (it != siblings.end())
        {
          siblings.insert(it + 1, obj);
          obj->parent = parent;
        }
      }
    }
  }

  return true;
}

void Project::Scene::save()
{
  Utils::FS::saveTextFile(scenePath + "/scene.json", serialize());
}

uint32_t Project::Scene::createPrefabFromObject(uint32_t uuid)
{
  auto obj = getObjectByUUID(uuid);
  if(!obj)return 0;

  Prefab prefab{};
  prefab.uuid.value = Utils::Hash::randomU64();
  auto prefabJson = prefab.serialize(*obj);

  std::string name = obj->name;

  name.erase(std::remove_if(name.begin(), name.end(),
    [](char c) { return !std::isalnum(c) && c != '_'; }
  ), name.end());
  if(name.empty())name = "prefab " + std::to_string(prefab.uuid.value);

  // Create the prefab in the folder the Prefabs browser is currently in (so prefabs can be
  // organized into subfolders), falling back to the assets root. Ensure the folder exists,
  // since saveTextFile does not create parent directories.
  std::filesystem::path targetDir = std::filesystem::path{ctx.project->getPath()} / "assets";
  if(!ctx.prefabCreateDir.empty()) {
    targetDir /= ctx.prefabCreateDir;
    std::error_code ec;
    std::filesystem::create_directories(targetDir, ec);
  }

  Utils::FS::saveTextFile(
    (targetDir / (name + ".prefab")).string(),
    prefabJson
  );

  // Defer the reload: this runs from an ImGui callback mid-frame, and reload() frees GPU
  // resources that draw commands already recorded this frame still reference.
  ctx.project->getAssets().requestReload();
  return 0;
}

std::string Project::Scene::serialize(bool minify) {
  nlohmann::json doc{};
  doc["conf"] = conf.serialize();
  doc["graph"] = root.serialize();
  return doc.dump(minify ? -1 : 2);
}

void Project::Scene::resetLayers()
{
  conf.layers3D.clear();
  conf.layersPtx.clear();
  conf.layers2D.clear();

  LayerConf layer{};
  layer.name.value = "3D Opaque";
  layer.depthCompare.value = true;
  layer.depthWrite.value = true;
  layer.blender.value = 0;
  conf.layers3D.push_back(layer);

  layer.name.value = "3D Transp.";
  layer.depthCompare.value = true;
  layer.depthWrite.value = false;
  layer.blender.value = RDPQ_BLENDER_MULTIPLY;
  conf.layers3D.push_back(layer);

  layer.name.value = "PTX Opaque";
  layer.depthCompare.value = true;
  layer.depthWrite.value = true;
  layer.blender.value = 0;
  conf.layersPtx.push_back(layer);

  layer.name.value = "2D";
  layer.depthCompare.value = false;
  layer.depthWrite.value = false;
  layer.blender.value = 0;
  conf.layers2D.push_back(layer);
}

void Project::Scene::deserialize(const std::string &data)
{
  auto doc = nlohmann::json::parse(
    !data.empty() ? data : "{\"conf\": {}}",
    nullptr, false);
  if (!doc.is_object())return;

  auto &docConf = doc["conf"];
  {
    Utils::JSON::readProp(docConf, conf.name, std::string{"New Scene"});
    conf.fbWidth = docConf.value("fbWidth", 320);
    conf.fbHeight = docConf.value("fbHeight", 240);
    conf.fbFormat = docConf.value("fbFormat", 0);
    Utils::JSON::readProp(docConf, conf.clearColor);
    Utils::JSON::readProp(docConf, conf.doClearColor);
    Utils::JSON::readProp(docConf, conf.doClearDepth);
    Utils::JSON::readProp(docConf, conf.renderPipeline);
    Utils::JSON::readProp(docConf, conf.frameLimit, 0);
    Utils::JSON::readProp(docConf, conf.filter, 0);
    Utils::JSON::readProp(docConf, conf.audioFreq, 32000);
    Utils::JSON::readProp(docConf, conf.physicsTickRate, 50);
    Utils::JSON::readProp(docConf, conf.gravity, glm::vec3{0.0f, -9.81f, 0.0f});
    Utils::JSON::readProp(docConf, conf.visualUnitsPerMeter, 100.0f);
    Utils::JSON::readProp(docConf, conf.velocitySolverIterations, 7);
    Utils::JSON::readProp(docConf, conf.positionSolverIterations, 6);
    Utils::JSON::readProp(docConf, conf.interpolatePhysicsTransforms, true);

    auto readLayer = [](const nlohmann::json &dom) {
      LayerConf layer{};
      Utils::JSON::readProp(dom, layer.name);
      Utils::JSON::readProp(dom, layer.depthCompare, true);
      Utils::JSON::readProp(dom, layer.depthWrite, true);
      Utils::JSON::readProp(dom, layer.blender);
      Utils::JSON::readProp(dom, layer.fog, false);
      Utils::JSON::readProp(dom, layer.fogColorMode, 0u);
      Utils::JSON::readProp(dom, layer.fogColor);
      Utils::JSON::readProp(dom, layer.fogMin, 0.0f);
      Utils::JSON::readProp(dom, layer.fogMax, 0.0f);
      Utils::JSON::readProp(dom, layer.lightMode, 0);

      return layer;
    };

    conf.layers3D.clear();
    conf.layersPtx.clear();
    conf.layers2D.clear();
    for(auto &item : docConf["layers3D"]) {
      conf.layers3D.push_back(readLayer(item));
    }
    for(auto &item : docConf["layersPtx"]) {
      conf.layersPtx.push_back(readLayer(item));
    }
    for(auto &item : docConf["layers2D"]) {
      conf.layers2D.push_back(readLayer(item));
    }
    if(conf.layers3D.empty()) {
      resetLayers();
    }
  }

  removeAllObjects();
  if(!doc.contains("graph"))return;
  auto docGraph = doc["graph"];
  root.deserialize(this, docGraph);

  // Sync prefab instance subobjects with their (possibly updated) prefab definitions.
  reconcilePrefabInstances();
}

void Project::Scene::assignRuntimeIds()
{
  // Pre-order traversal: parents get a lower id than their children, root stays 0.
  // Ids are unique per scene and only valid for this build.
  uint32_t nextId = 1;
  auto assign = [&nextId](const std::shared_ptr<Object> &obj, auto &assignRef) -> void
  {
    if(nextId > 0xFFFF) {
      Utils::Logger::log("Scene has more than 65535 objects, runtime ids overflow", Utils::Logger::LEVEL_ERROR);
      return;
    }
    obj->runtimeId = static_cast<uint16_t>(nextId++);
    for(const auto &child : obj->children) {
      assignRef(child, assignRef);
    }
  };

  root.runtimeId = 0;
  for(const auto &child : root.children) {
    assign(child, assign);
  }
}
