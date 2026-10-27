/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "selectionUtils.h"

#include <algorithm>

#include "../context.h"
#include "../project/scene/scene.h"
#include "../project/scene/prefab.h"

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
    if(ctx.isPrefabEditing(rootInstance->uuid)) {
      // The first instance whose definition the path descends into (excluding the target
      // itself). Objects added to an instance stay in the edited file, so those steps
      // don't count.
      int ai = -1;
      for(size_t i = 0; i + 1 < steps.size(); i++) {
        if(steps[i].node->isPrefabInstance() && !steps[i + 1].additive) { ai = (int)i; break; }
      }
      if(ai >= 0) {
        out.authNode = steps[ai].node;
        out.relPath.assign(ctx.selSubPath.begin() + ai + 1, ctx.selSubPath.end());
      } else {
        out.directDefEdit = true;
      }
    } else {
      // The build writes an object added to a nested instance with a fresh scope, so the
      // scene instance's overrides never reach it or anything below it. Show it the way the
      // build resolves it: from the last such object down, without any override to edit.
      int last = -1;
      for(size_t i = 0; i < steps.size(); i++) {
        if(steps[i].additive) last = (int)i;
      }
      if(last >= 0) {
        out.readOnly = true;
        out.authNode = steps[last].node;
        out.relPath.assign(ctx.selSubPath.begin() + last + 1, ctx.selSubPath.end());
      }
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
