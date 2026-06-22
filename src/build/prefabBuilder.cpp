/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "projectBuilder.h"
#include "../utils/string.h"
#include "../utils/fs.h"
#include "../utils/logger.h"
#include "../utils/proc.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
  // Assign pre-order runtime ids (root=1, then children) to the prefab tree so the
  // serialized parent links (group = parent's runtimeId) are consistent, and the runtime
  // can remap them when spawning the prefab. Returns the total object count.
  uint32_t assignPrefabRuntimeIds(Project::Object &obj, Project::Object *parent, uint32_t &nextId)
  {
    obj.parent = parent;
    obj.runtimeId = static_cast<uint16_t>(nextId++);
    uint32_t count = 1;
    for (auto &child : obj.children) {
      count += assignPrefabRuntimeIds(*child, &obj, nextId);
    }
    return count;
  }
}

bool Build::buildPrefabAssets(Project::Project &project, SceneCtx &sceneCtx)
{
  auto &assets = sceneCtx.project->getAssets().getTypeEntries(Project::FileType::PREFAB);
  for (auto &asset : assets)
  {
    if(asset.conf.exclude)continue;

    auto projectPath = fs::path{project.getPath()};
    auto outPath = projectPath / asset.outPath;
    auto outDir = outPath.parent_path();
    fs::create_directories(outPath.parent_path());

    sceneCtx.files.push_back(Utils::FS::toUnixPath(asset.outPath));

    // @TODO: lazy-build again after refactoring the asset table building
    //if(!assetBuildNeeded(asset, outPath))continue;

    sceneCtx.fileObj = {};
    // Header: number of objects in this prefab, so the runtime can instantiate the full
    // hierarchy (root + all children), not just the root. Ids are assigned pre-order to
    // keep parent/group links resolvable and remappable at spawn time.
    uint32_t idCounter = 1;
    uint32_t prefabObjCount = assignPrefabRuntimeIds(asset.prefab->obj, nullptr, idCounter);
    sceneCtx.fileObj.write<uint32_t>(prefabObjCount);
    writeObject(sceneCtx, asset.prefab->obj, true);
    sceneCtx.fileObj.writeToFile(outPath);
    sceneCtx.fileObj = {};
  }

  return true;
}