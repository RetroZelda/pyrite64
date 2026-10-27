/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "../utils/prop.h"
#include "imgui/helper.h"

namespace Project {
  class Scene;
  class Object;
  class Prefab;
}

namespace Editor::SelectionUtils
{
  std::vector<Project::Object*> collectSelectedObjects(Project::Scene &scene);

  bool deleteSelectedObjects(Project::Scene &scene);

  // The scene object currently in prefab-edit mode, or nullptr. While one is active,
  // selection is restricted to that prefab's own definition. See isSelectionAllowed.
  Project::Object* getPrefabEditObject(Project::Scene &scene);

  // Whether a selection may target rootUuid plus the nested subPath in the current mode.
  // With no edit mode anything is allowed. In edit mode only the edited prefab's own
  // definition is selectable, meaning the instance itself, plain nested children, and
  // nested prefab-instance nodes. The contents of those nested prefabs are not, so a
  // prefab-instance ancestor on the path makes the node unselectable.
  bool isSelectionAllowed(Project::Scene &scene, uint32_t rootUuid,
                          const std::vector<uint32_t> &subPath);

  // Resolves a node to the prefab definition whose children it exposes. A prefab-instance
  // node exposes the referenced prefab's root, otherwise the node itself (also when the
  // prefab cannot be found). An instance node can still hold children of its own, see
  // NestedStep::additive.
  Project::Object* prefabDefOf(Project::Object* node);

  // One element of a nested selection path, see walkNested().
  struct NestedStep
  {
    Project::Object* node = nullptr;
    // Found among the parent's OWN children rather than its prefab definition's: an object
    // added directly to a nested prefab instance. It belongs to the same file as that
    // instance, and the build writes it with a fresh override scope (its own overrides only).
    bool additive = false;
  };

  // Follows a sub-path of child ids from a prefab root down to the selected node, through each
  // prefab definition along the way and into objects added directly to nested instances.
  // Returns one step per path element, or an empty vector when the path no longer resolves.
  std::vector<NestedStep> walkNested(Project::Object* root, const std::vector<uint32_t> &subPath);

  // Where a nested node's edits are written, given the chain of def nodes leading to it.
  struct AuthTarget
  {
    Project::Object* authNode = nullptr;  // object whose override map the edit is stored on
    std::vector<uint32_t> relPath;        // path from authNode down to the edited node
    bool directDefEdit = false;           // edit the def node directly, no override layer
    // No edit can reach the node from here: outside prefab-edit mode, an object added
    // directly to a nested instance (or anything below it) ignores the scene instance's
    // overrides in the build. authNode/relPath then only describe how to display it.
    bool readOnly = false;
  };

  // Decides where a nested node's edits go. In normal mode that is the scene instance with
  // the full selSubPath, or nowhere (readOnly) below an object added to a nested instance.
  // In prefab-edit mode it is the first contained prefab-instance whose definition the path
  // descends into, whose overrides belong to the prefab being edited, or a direct definition
  // edit when the path stays in the edited prefab's file.
  AuthTarget pickAuthNode(Project::Object* rootInstance, const std::vector<NestedStep> &steps);

  // A nested prefab selection resolved for the inspector: which def node to show, and the
  // live scope guards that route its edits to the correct override owner. The guards are
  // non-movable, so the caller keeps this as a local for as long as it renders the node.
  struct NestedTarget
  {
    Project::Object* node = nullptr;     // inspected nested def node, null when not nested
    Project::Object* nodeSrc = nullptr;  // its component source, its prefab root if instance
    bool isNested = false;
    bool directDefEdit = false;
    bool readOnly = false;               // see AuthTarget::readOnly
    std::optional<PropScope::PrefabLayer> authLayer;
    std::vector<std::unique_ptr<PropScope::Path>> nestedPaths;
    std::optional<ImTable::ForceLockScope> nestedLock;
    // One layer per prefab-instance node between the author node and the selected node (the selected node's own layer is the last).
    // Keeps the inspector's cascade matching the build.
    std::vector<std::unique_ptr<PropScope::PrefabLayer>> nodeLayers;
  };

  // Prefab-edit mode structure edits. They only act on nodes of the edited prefab's own file
  // (see isSelectionAllowed), identified by the edited instance and the path to the node.

  // The node of the edited prefab at rootUuid + path (the prefab root for an empty path), or
  // null when it is not part of the edited prefab's file. prefabUUID receives the prefab.
  Project::Object* findEditedPrefabNode(Project::Scene &scene, uint32_t rootUuid,
                                        const std::vector<uint32_t> &path, uint64_t &prefabUUID);

  // Adds a new object (or a "Spline Path") under the node at path, selects it and returns its
  // path. Under a nested instance node it becomes an object added to that instance. Empty when
  // nothing was added.
  std::vector<uint32_t> addPrefabObject(Project::Scene &scene, uint32_t rootUuid,
                                        std::vector<uint32_t> path, bool splinePath);

  // Removes the node at path from the edited prefab and selects its parent.
  bool removePrefabObject(Project::Scene &scene, uint32_t rootUuid, std::vector<uint32_t> path);

  // Decides where a nested node's edits are written and sets up the matching scope guards.
  // In normal mode that is the scene instance with the full path. In prefab-edit mode it is
  // the first contained prefab-instance along the path, whose overrides belong to the prefab
  // being edited, or a direct definition edit when no such prefab sits in between. Fills
  // 'out' in place to keep the guards alive.
  void resolveNestedTarget(NestedTarget &out, Project::Object* obj, Project::Prefab* prefab);
}
