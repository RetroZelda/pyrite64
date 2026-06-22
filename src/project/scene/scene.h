/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "object.h"

namespace Project
{
  struct LayerConf
  {
    PROP_STRING(name);
    PROP_BOOL(depthCompare);
    PROP_BOOL(depthWrite);
    PROP_U32(blender);
    PROP_BOOL(fog);
    PROP_U32(fogColorMode);
    PROP_VEC4(fogColor);
    PROP_FLOAT(fogMin);
    PROP_FLOAT(fogMax);
    PROP_S32(lightMode);
  };

  struct SceneConf
  {
    PROP_STRING(name);
    int fbWidth{320};
    int fbHeight{240};
    int fbFormat{0};
    PROP_VEC4(clearColor);
    PROP_BOOL(doClearColor);
    PROP_BOOL(doClearDepth);
    PROP_S32(renderPipeline);
    PROP_S32(frameLimit);
    PROP_S32(filter);
    PROP_S32(audioFreq);
    PROP_S32(physicsTickRate);
    PROP_VEC3(gravity);
    PROP_FLOAT(visualUnitsPerMeter);
    PROP_S32(velocitySolverIterations);
    PROP_S32(positionSolverIterations);
    PROP_BOOL(interpolatePhysicsTransforms);

    std::vector<LayerConf> layers3D{};
    std::vector<LayerConf> layersPtx{};
    std::vector<LayerConf> layers2D{};

    nlohmann::json serialize() const;
  };

  class Scene
  {
    private:
      int id{};
      Object root{};
      std::string scenePath{};

      // Recursively removes an object and all its descendants from objectsMap.
      void unregisterObjectTree(const Object &obj);

      // Removes an object (and its descendants) from both objectsMap and its parent's
      // children list. The reference is invalid after this call.
      void removeObjectAndChildren(Object &obj);

      // Materializes an instance node (and its full subtree) from a template node within
      // ownerPrefabUUID, attached under `parent`. Linked back via uuidPrefab/uuidPrefabNode.
      std::shared_ptr<Object> materializeInstanceNode(Object &parent, uint64_t ownerPrefabUUID, Object &templateNode, int depth);

      // Syncs an instance node's children against its template from BOTH sources: the resolved
      // inner-prefab content, and (for a nested-reference template node) the additive children
      // stored on the reference node itself in the owner prefab.
      void syncInstanceChildren(Object &inst, uint64_t ownerPrefabUUID, Object &templateNode, int depth);

      // Syncs the subset of inst.children owned by setOwnerPrefabUUID against templateChildren
      // (add missing, remove orphaned, refresh + recurse existing).
      void syncChildSet(Object &inst, uint64_t setOwnerPrefabUUID,
        const std::vector<std::shared_ptr<Object>> &templateChildren, int depth);

    public:
      SceneConf conf{};

      Scene(int id_, const std::string &projectPath);

      int getId() const { return id; }
      const std::string &getName() const { return conf.name.value; }

      void save();
      Object& getRootObject() { return root; }

      std::unordered_map<uint32_t, std::shared_ptr<Object>> objectsMap{};

      std::shared_ptr<Object> addObject(std::string &objJson, uint64_t parentUUID = 0);
      std::shared_ptr<Object> addObject(Object &parent);
      std::shared_ptr<Object> addObject(Object &parent, std::shared_ptr<Object> obj, bool generateUUID = false);

      std::shared_ptr<Object> addPrefabInstance(uint64_t prefabUUID);
      std::shared_ptr<Object> addPrefabInstance(uint64_t prefabUUID, Object& parent);

      // Walks the whole scene and syncs every prefab instance's subobject tree against
      // its current prefab definition. Safe to call repeatedly; a no-op when in sync.
      // Run on scene load and at build time so prefab edits propagate to instances.
      void reconcilePrefabInstances();

      // --- Prefab template editing (structural edits go to the prefab, not the instance) ---

      // True if the node is a prefab instance subobject (i.e. not the placement root).
      bool isPrefabSubobject(const Object &o) const;
      // True only if the node represents an actual INSTANCE of a prefab: the placed scene
      // instance, or a nested-prefab REFERENCE node. Plain materialized subobjects carry
      // uuidPrefab=owner (for reconcile) but are NOT prefabs — use this (not isPrefabInstance)
      // for prefab UI (package icon, "Edit" button) and edit-mode toggling.
      bool isPrefabInstanceRoot(const Object &o) const;
      // True if the prefab this node belongs to is currently being edited (any node of the
      // instance has isPrefabEdit set). Coarse (whole-instance) check; prefer resolveAddTarget
      // for per-level routing.
      bool isEditingPrefabOf(const Object &o) const;

      // --- Per-level edit scope (nested prefab-in-prefab) ---

      // Where a child dropped/added under a scene node should be stored, honoring per-level
      // edit scope: the innermost prefab in edit mode along the node's chain.
      struct AddTarget {
        Object* parent{nullptr};  // template node to add the child under (null => not allowed)
        uint64_t prefabUUID{0};   // prefab whose definition is modified (mark dirty)
        bool additive{false};     // additive override on the owner's copy of a nested instance
      };

      // Nearest ancestor-or-self that is the in-scene instance ROOT representing prefabUUID.
      Object* instanceRootForPrefab(Object &node, uint64_t prefabUUID);
      // Resolves where a child added under `node` lands (see AddTarget / the per-level rule).
      AddTarget resolveAddTarget(Object &node);
      // Resolves the template node whose ATTRIBUTES should be edited for instance `node`
      // (`parent` = that node; `prefabUUID` = prefab to mark dirty). parent==null => locked.
      AddTarget resolveEditTarget(Object &node);

      // Embeds an instance of innerPrefabUUID under destInstNode, routed via resolveAddTarget
      // (into the inner prefab's definition, or as an additive override on the owner prefab).
      // Returns the freshly materialized child, or nullptr if not allowed.
      std::shared_ptr<Object> addPrefabReferenceToPrefab(Object &destInstNode, uint64_t innerPrefabUUID);

      // Adds a new empty subobject to the prefab template node mirrored by instNode, then
      // reconciles. Returns the freshly materialized child under instNode, or nullptr.
      std::shared_ptr<Object> prefabAddChild(Object &instNode);
      // Removes the prefab template node mirrored by instSubobj (never the prefab root).
      void prefabRemoveNode(Object &instSubobj);
      // Prefab-aware drag handling (reparent within / drag in / drag out).
      // Returns: 0 = not a prefab move (caller should fall back to moveObject),
      //          1 = handled and changed something, 2 = handled but intentionally ignored.
      int prefabMove(Object *src, Object *tgt, bool asChild);

      // Writes the current transform of a materialized prefab-instance node and all of its
      // managed descendants back into their prefab template slots, then marks the affected
      // prefabs dirty. Used when transform-editing subobjects while editing a prefab, so the
      // change persists and propagates to every instance (reconcile re-applies it).
      void commitInstanceSubtreeToPrefab(Object &node);

      void removeObject(Object &obj);
      void removeAllObjects();

      bool moveObject(uint32_t uuidObject, uint32_t uuidTarget, bool asChild);

      std::shared_ptr<Object> getObjectByUUID(uint32_t uuid) {
        if (objectsMap.contains(uuid)) {
          return objectsMap[uuid];
        }
        return nullptr;
      }

      uint32_t createPrefabFromObject(uint32_t uuid);

      std::string serialize(bool minify = false);

      void resetLayers();

      void deserialize(const std::string &data);

      // Assigns the runtime object ids (uint16_t) for the whole tree.
      // Build-time only: must be called before serializing objects to the runtime format.
      void assignRuntimeIds();
  };
}
