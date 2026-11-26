/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <vector>

#include "stringTable.h"
#include "../utils/binaryFile.h"
#include "../utils/toolchain.h"
#include "../project/events/eventRules.h"
#include "../project/locale/localization.h"

namespace Project { class Project; class Scene; class Object; struct AssetManagerEntry; }

namespace Build
{
  struct AssetEntry
  {
    std::string path{};
    uint32_t stringOffset{};
    uint32_t type{};
    uint32_t flags{};
    // 3D models: meters per quantized vertex unit (1 / import base-scale), 0 otherwise
    float vertexScale{};
  };

  struct SceneCtx
  {
    Utils::Toolchain toolchain{};
    Project::Project *project{};
    Project::Scene *scene{};
    std::vector<std::string> files{};
    std::array<uint64_t, 16> autoLoadFontUUIDs{};
    std::unordered_map<uint64_t, uint32_t> codeIdxMapUUID{};
    Utils::BinaryFile fileScene{};
    Utils::BinaryFile fileObj{};
    StringTable strTable{};
    std::vector<uint64_t> graphFunctions{};

    std::vector<AssetEntry> assetList{};
    std::unordered_map<uint64_t, uint32_t> assetUUIDToIdx{};
    std::string assetFileMap{};
    uint32_t stringOffset{0};

    // Next free runtime object id. Advances as prefab instances are expanded.
    uint32_t nextRuntimeId{1};

    bool needsOpus{false};
    bool hasCanvases{false};

    // Fork: event graph, see eventBuilder.h
    Project::Events::Catalog eventCatalog{};
    Project::Events::Index eventIndex{};
    std::vector<Project::Events::Message> eventMessages{};
    std::string eventWhere{}; // "scene 3" or "prefab Door.prefab", for messages

    // Where the object being written sits and which graph wrote its bindings (PLAN 3.4).
    struct EventPathEntry { uint32_t uuid; bool prefabRoot; const std::string *name; };
    struct EventContext
    {
      int rootIndex;              // index in eventPath of this graph's root (its instance root), -1: the scene
      Project::Object *instance;  // the instance node of this graph (holds the outer graph's overrides)
      bool isScene;
      bool spawnedRoot;           // the root of a prefab built for spawning
    };
    std::vector<EventPathEntry> eventPath{};
    std::vector<EventContext> eventContexts{};

    // Fork: localization. Keys of LocText fields, written by the scenes and prefabs built so far.
    Project::Loc::Uses locUses{};

    void addAsset(const Project::AssetManagerEntry &entry);
  };
}
