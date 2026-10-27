/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "selectionUtils.h"

#include <algorithm>
#include <filesystem>

#include "../context.h"
#include "../project/scene/scene.h"
#include "../project/scene/prefab.h"
#include "../project/component/components.h"
#include "../project/component/types/compSpline.h"
#include "../utils/hash.h"
#include "../utils/logger.h"
#include "imgui/notification.h"

namespace
{
  std::vector<std::shared_ptr<Project::Object>> collectSelectedObjectRefs(Project::Scene &scene)
  {
    const auto &selected = ctx.getSelectedObjectUUIDs();
    std::vector<std::shared_ptr<Project::Object>> selectedObjects{};
    selectedObjects.reserve(selected.size());
    for (auto uuid : selected) {
      auto obj = scene.getObjectByUUID(uuid);
      if (obj) {
        selectedObjects.push_back(obj);
      }
    }
    return selectedObjects;
  }

}

namespace Editor::SelectionUtils
{
  Project::Object* prefabDefOf(Project::Object* node)
  {
    if(node->isPrefabInstance()) {
      auto pf = ctx.project->getAssets().getPrefabByUUID(node->uuidPrefab.value);
      if(pf) return &pf->obj;
    }
    return node;
  }

  std::vector<NestedStep> walkNested(Project::Object* root, const std::vector<uint32_t> &subPath)
  {
    std::vector<NestedStep> steps;
    Project::Object* cur = root;
    for(uint32_t uid : subPath) {
      Project::Object* parent = prefabDefOf(cur);
      NestedStep step{};
      for(auto &c : parent->children) { if(c->uuid == uid) { step.node = c.get(); break; } }
      if(!step.node && parent != cur) { // an object added directly to the instance node
        for(auto &c : cur->children) { if(c->uuid == uid) { step.node = c.get(); break; } }
        step.additive = step.node != nullptr;
      }
      if(!step.node) return {};
      steps.push_back(step);
      cur = step.node;
    }
    return steps;
  }

  AuthTarget pickAuthNode(Project::Object* rootInstance, const std::vector<NestedStep> &steps)
  {
    AuthTarget out;
    out.authNode = rootInstance;
    out.relPath = ctx.selSubPath;

    // The build writes an object added to a nested instance with a fresh scope, so no override
    // from above (the scene instance, or an instance of the edited prefab) reaches it or anything
    // below it. Such a node is shown the way the build resolves it, from the last added object
    // down, without any override to edit.
    int lastAdded = -1;
    for(size_t i = 0; i < steps.size(); i++) {
      if(steps[i].additive) lastAdded = (int)i;
    }
    auto showReadOnly = [&]() {
      out.readOnly = true;
      out.authNode = steps[lastAdded].node;
      out.relPath.assign(ctx.selSubPath.begin() + lastAdded + 1, ctx.selSubPath.end());
    };

    if(ctx.isPrefabEditing(rootInstance->uuid)) {
      // The first instance whose definition the path descends into (excluding the target
      // itself). Objects added to an instance stay in the edited file, so those steps
      // don't count.
      int ai = -1;
      for(size_t i = 0; i + 1 < steps.size(); i++) {
        if(steps[i].node->isPrefabInstance() && !steps[i + 1].additive) { ai = (int)i; break; }
      }
      if(ai < 0) {
        out.directDefEdit = true; // the path stays in the edited prefab's file
      } else if(lastAdded > ai) {
        showReadOnly();           // added inside the nested prefab: its overrides never get there
      } else {
        out.authNode = steps[ai].node;
        out.relPath.assign(ctx.selSubPath.begin() + ai + 1, ctx.selSubPath.end());
      }
    } else if(lastAdded >= 0) {
      showReadOnly();
    }
    return out;
  }

  std::vector<Project::Object*> collectSelectedObjects(Project::Scene &scene)
  {
    const auto &selected = ctx.getSelectedObjectUUIDs();
    std::vector<Project::Object*> selectedObjects{};
    selectedObjects.reserve(selected.size());
    for (auto uuid : selected) {
      auto obj = scene.getObjectByUUID(uuid);
      if (obj) {
        selectedObjects.push_back(obj.get());
      }
    }
    return selectedObjects;
  }

  bool deleteSelectedObjects(Project::Scene &scene)
  {
    // A nested prefab node is selected through its instance, which stays in the selection
    // list: deleting "the selection" would remove the whole instance.
    if (!ctx.selSubPath.empty()) return false;

    auto selectedRefs = collectSelectedObjectRefs(scene);
    if (selectedRefs.empty()) {
      return false;
    }

    std::vector<std::shared_ptr<Project::Object>> selectedObjs{};
    selectedObjs.reserve(selectedRefs.size());
    for (auto &selObj : selectedRefs) {
      if (!selObj || !selObj->parent) continue;
      selectedObjs.push_back(selObj);
    }

    if (selectedObjs.empty()) {
      return false;
    }

    auto depthOf = [](Project::Object *obj) {
      int depth = 0;
      while (obj && obj->parent) {
        ++depth;
        obj = obj->parent;
      }
      return depth;
    };

    std::sort(selectedObjs.begin(), selectedObjs.end(), [&](
      const std::shared_ptr<Project::Object> &a,
      const std::shared_ptr<Project::Object> &b
    ) {
      return depthOf(a.get()) > depthOf(b.get());
    });

    for (auto &selObj : selectedObjs) {
      if (!selObj || !selObj->parent) continue;
      scene.removeObject(*selObj);
    }
    ctx.clearObjectSelection();
    return true;
  }

  Project::Object* getPrefabEditObject(Project::Scene &scene)
  {
    if (!ctx.prefabEditUUID) return nullptr;
    auto obj = scene.getObjectByUUID(ctx.prefabEditUUID);
    return obj ? obj.get() : nullptr;
  }

  bool isSelectionAllowed(Project::Scene &scene, uint32_t rootUuid,
                          const std::vector<uint32_t> &subPath)
  {
    auto *editObj = getPrefabEditObject(scene);
    if (!editObj) return true;                       // not editing, anything goes
    if (subPath.empty()) return rootUuid == editObj->uuid;
    if (rootUuid != editObj->uuid) return false;     // a different object's tree

    auto prefab = ctx.project->getAssets().getPrefabByUUID(editObj->uuidPrefab.value);
    if (!prefab) return false;

    auto steps = walkNested(&prefab->obj, subPath);
    if (steps.empty()) return false;                 // stale path

    // Descending into a nested prefab instance's definition means we'd be selecting inside
    // that prefab, which is not part of this prefab's file. Objects added directly to the
    // instance are.
    for (size_t i = 1; i < steps.size(); i++) {
      if (steps[i - 1].node->isPrefabInstance() && !steps[i].additive) return false;
    }
    return true;
  }

  Project::Object* findEditedPrefabNode(Project::Scene &scene, uint32_t rootUuid,
                                        const std::vector<uint32_t> &path, uint64_t &prefabUUID)
  {
    if (!ctx.isPrefabEditing(rootUuid) || !isSelectionAllowed(scene, rootUuid, path)) return nullptr;
    auto root = scene.getObjectByUUID(rootUuid);
    if (!root) return nullptr;
    auto prefab = ctx.project->getAssets().getPrefabByUUID(root->uuidPrefab.value);
    if (!prefab) return nullptr;
    prefabUUID = prefab->uuid.value;
    if (path.empty()) return &prefab->obj;
    auto steps = walkNested(&prefab->obj, path);
    return steps.empty() ? nullptr : steps.back().node;
  }

  std::vector<uint32_t> addPrefabObject(Project::Scene &scene, uint32_t rootUuid,
                                        std::vector<uint32_t> path, bool splinePath)
  {
    uint64_t prefabUUID = 0;
    auto *parent = findEditedPrefabNode(scene, rootUuid, path, prefabUUID);
    if (!parent) return {};

    // A plain node takes it as a child, a nested instance node as an object added to it:
    // both live in the node's own children list, in the edited prefab's file.
    auto child = std::make_shared<Project::Object>(*parent);
    do { child->uuid = static_cast<uint32_t>(Utils::Hash::randomU64()); } while (child->uuid == 0);
    child->name = splinePath ? "Spline Path" : "New Object";
    child->rot.value = {0,0,0,1};
    child->scale.value = {1,1,1};
    if (splinePath) child->addComponent(Project::Component::Spline::ID);
    parent->children.push_back(child);

    ctx.project->getAssets().markPrefabDirty(prefabUUID);
    path.push_back(child->uuid);
    ctx.setNestedSelection(rootUuid, path);
    return path;
  }

  bool removePrefabObject(Project::Scene &scene, uint32_t rootUuid, std::vector<uint32_t> path)
  {
    if (path.empty()) return false; // the prefab root goes with its instance, not here
    uint64_t prefabUUID = 0;
    auto *node = findEditedPrefabNode(scene, rootUuid, path, prefabUUID);
    if (!node) return false;

    // Within the edited file every node sits in its parent's own children list (a plain
    // child, or an object added to a nested instance), see isSelectionAllowed.
    path.pop_back();
    auto *parent = findEditedPrefabNode(scene, rootUuid, path, prefabUUID);
    if (!parent) return false;
    auto &siblings = parent->children;
    auto it = std::find_if(siblings.begin(), siblings.end(), [node](const auto &c) { return c.get() == node; });
    if (it == siblings.end()) return false;
    siblings.erase(it);

    ctx.project->getAssets().markPrefabDirty(prefabUUID);
    if (path.empty()) ctx.setObjectSelection(rootUuid);
    else ctx.setNestedSelection(rootUuid, path);
    return true;
  }

  std::vector<uint32_t> nestPrefabInstance(Project::Scene &scene, uint32_t rootUuid,
                                           std::vector<uint32_t> path, uint64_t prefabUUID)
  {
    uint64_t editedUUID = 0;
    auto *parent = findEditedPrefabNode(scene, rootUuid, path, editedUUID);
    if (!parent) return {};
    auto &assets = ctx.project->getAssets();
    auto prefab = assets.getPrefabByUUID(prefabUUID);
    if (!prefab) return {};

    if (assets.prefabContains(prefabUUID, editedUUID)) {
      auto msg = "'" + prefab->obj.name + "' can't be nested here: it is or contains the prefab being edited";
      Utils::Logger::log(msg, Utils::Logger::LEVEL_WARN);
      Editor::Noti::add(Editor::Noti::Type::ERROR, msg);
      return {};
    }

    // Set up like a scene instance (Scene::addPrefabInstance), placed at the parent's origin
    auto child = std::make_shared<Project::Object>(*parent);
    do { child->uuid = static_cast<uint32_t>(Utils::Hash::randomU64()); } while (child->uuid == 0);
    child->name = prefab->obj.name;
    child->pos.value = {0,0,0};
    child->rot.value = {0,0,0,1};
    child->scale = prefab->obj.scale;
    child->uuidPrefab.value = prefabUUID;
    child->addPropOverride(child->pos); // the instance's placement
    child->addPropOverride(child->rot);
    child->addPropOverride(child->scale);
    parent->children.push_back(child);

    assets.markPrefabDirty(editedUUID);
    path.push_back(child->uuid);
    ctx.setNestedSelection(rootUuid, path);
    return path;
  }

  std::vector<uint32_t> addPrefabModelObject(Project::Scene &scene, uint32_t rootUuid,
                                             const std::vector<uint32_t> &path, uint64_t modelUUID)
  {
    constexpr int COMP_ID_MODEL_STATIC = 1;    // see Scene::addModelObject
    constexpr int COMP_ID_MODEL_ANIMATED = 10;

    auto *asset = ctx.project->getAssets().getEntryByUUID(modelUUID);
    if (!asset || asset->type != Project::FileType::MODEL_3D) return {};
    auto added = addPrefabObject(scene, rootUuid, path, false);
    uint64_t prefabUUID = 0;
    auto *node = added.empty() ? nullptr : findEditedPrefabNode(scene, rootUuid, added, prefabUUID);
    if (!node) return {};

    node->name = std::filesystem::path{asset->name}.stem().string();
    // Models containing animation clips use the animated component automatically
    bool isAnimated = !asset->model.t3dm.animations.empty();
    node->addComponent(isAnimated ? COMP_ID_MODEL_ANIMATED : COMP_ID_MODEL_STATIC);
    auto &component = node->components.back();
    if (isAnimated) Project::Component::AnimModel::setModel(component, modelUUID);
    else            Project::Component::Model::setModel(component, modelUUID);

    ctx.project->getAssets().markPrefabDirty(prefabUUID);
    return added;
  }

  std::vector<uint32_t> movePrefabObject(Project::Scene &scene, uint32_t rootUuid,
                                         std::vector<uint32_t> path,
                                         const std::vector<uint32_t> &newParentPath)
  {
    if (path.empty()) return {};
    // not into itself or anything below it
    if (newParentPath.size() >= path.size()
        && std::equal(path.begin(), path.end(), newParentPath.begin())) return {};

    uint64_t editedUUID = 0;
    auto *node = findEditedPrefabNode(scene, rootUuid, path, editedUUID);
    auto oldParentPath = path;
    oldParentPath.pop_back();
    auto *oldParent = findEditedPrefabNode(scene, rootUuid, oldParentPath, editedUUID);
    auto *newParent = findEditedPrefabNode(scene, rootUuid, newParentPath, editedUUID);
    if (!node || !oldParent || !newParent || newParent == oldParent) return {};

    auto prefab = ctx.project->getAssets().getPrefabByUUID(editedUUID);
    if (!prefab) return {};

    // Placement within the prefab: compose the local transforms below its root down a path
    struct Trans { glm::vec3 pos{0.0f}; glm::quat rot{1,0,0,0}; glm::vec3 scale{1.0f}; };
    auto placementOf = [&](const std::vector<uint32_t> &p) {
      Trans t{};
      for (auto &step : walkNested(&prefab->obj, p)) {
        auto *n = step.node;
        glm::vec3 lpos = n->pos.resolve(n->propOverrides);
        glm::quat lrot = n->rot.resolve(n->propOverrides);
        glm::vec3 lscale = n->scale.resolve(n->propOverrides);
        t = Trans{t.pos + t.rot * (t.scale * lpos), t.rot * lrot, t.scale * lscale};
      }
      return t;
    };
    Trans nodeT = placementOf(path);
    Trans parentT = placementOf(newParentPath);
    for (int i = 0; i < 3; i++) if (glm::abs(parentT.scale[i]) < 0.0001f) parentT.scale[i] = 0.0001f;

    glm::quat pInv = glm::inverse(parentT.rot);
    node->pos.resolve(node->propOverrides) = (pInv * (nodeT.pos - parentT.pos)) / parentT.scale;
    node->rot.resolve(node->propOverrides) = pInv * nodeT.rot;
    node->scale.resolve(node->propOverrides) = nodeT.scale / parentT.scale;

    auto &siblings = oldParent->children;
    auto it = std::find_if(siblings.begin(), siblings.end(), [node](const auto &c) { return c.get() == node; });
    if (it == siblings.end()) return {};
    auto moved = *it;
    siblings.erase(it);
    moved->parent = newParent;
    newParent->children.push_back(moved);

    ctx.project->getAssets().markPrefabDirty(editedUUID);
    auto newPath = newParentPath;
    newPath.push_back(moved->uuid);
    ctx.setNestedSelection(rootUuid, newPath);
    return newPath;
  }

  void resolveNestedTarget(NestedTarget &out, Project::Object* obj, Project::Prefab* prefab)
  {
    if(!prefab || ctx.selSubPath.empty()) return;

    auto steps = walkNested(&prefab->obj, ctx.selSubPath);
    if(steps.empty()) { ctx.selSubPath.clear(); return; } // stale path, fall back to the root

    out.node = steps.back().node;
    out.nodeSrc = prefabDefOf(out.node);
    out.isNested = true;

    auto auth = pickAuthNode(obj, steps);
    out.directDefEdit = auth.directDefEdit;
    out.readOnly = auth.readOnly;

    if(!out.directDefEdit) {
      out.authLayer.emplace(auth.authNode->propOverrides);
      out.nestedLock.emplace(true); // edits become overrides on authNode

      // Descend relPath, pushing one Path per step and a layer for each nested prefab-instance
      // node along the way (the selected node's own layer is the last). This resolves the same
      // cascade the build does, so an override baked on an intermediate prefab still shows.
      // authNode stays the outermost layer, so it remains the write target.
      size_t startIdx = steps.size() - auth.relPath.size();
      for(size_t k = 0; k < auth.relPath.size(); k++) {
        out.nestedPaths.push_back(std::make_unique<PropScope::Path>(auth.relPath[k]));
        Project::Object* n = steps[startIdx + k].node;
        if(n->isPrefabInstance())
          out.nodeLayers.push_back(std::make_unique<PropScope::PrefabLayer>(n->propOverrides));
      }
    }
  }
}
