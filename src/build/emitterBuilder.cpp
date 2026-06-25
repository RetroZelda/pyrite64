/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "projectBuilder.h"

#include <filesystem>
#include "../utils/fs.h"
#include "../utils/binaryFile.h"
#include "../project/assets/emitter.h"

namespace fs = std::filesystem;

bool Build::buildEmitterAssets(Project::Project &project, SceneCtx &sceneCtx)
{
  auto &emitters = sceneCtx.project->getAssets().getTypeEntries(Project::FileType::EMITTER);
  for (auto &entry : emitters)
  {
    if (entry.conf.exclude || !entry.emitter) continue;

    sceneCtx.files.push_back(Utils::FS::toUnixPath(entry.outPath));

    auto outPath = fs::path{project.getPath()} / entry.outPath;
    fs::create_directories(outPath.parent_path());

    // Always rebuild: the .emit64 embeds ROM asset indices (texture lookups) that can shift
    // between builds, so an mtime check against the source .emitter is not sufficient.
    Utils::BinaryFile bf{};
    entry.emitter->build(bf, sceneCtx);
    bf.writeToFile(outPath);
  }
  return true;
}
