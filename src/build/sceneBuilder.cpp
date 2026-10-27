/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "projectBuilder.h"
#include "../utils/string.h"
#include <filesystem>
#include <algorithm>
#include <optional>
#include <stdexcept>

#include "../utils/binaryFile.h"
#include "../utils/fs.h"
#include "../utils/logger.h"

#include "engine/include/scene/objectFlags.h"
#include "../project/component/types/compSpline.h"

namespace T3D
{
 #include "tiny3d/tools/gltf_importer/src/math/quantizer.h"
}

namespace fs = std::filesystem;

namespace
{
  constexpr uint32_t FLAG_CLR_DEPTH = 1 << 0;
  constexpr uint32_t FLAG_CLR_COLOR = 1 << 1;
  constexpr uint32_t FLAG_SCR_32BIT = 1 << 2;
}

uint32_t Build::writeObject(Build::SceneCtx &ctx, Project::Object &obj, bool savePrefabItself,
                            uint16_t runtimeId, uint16_t parentRuntimeId, bool expanding,
                            const Build::WorldTransform &parentTransform, bool isPrefabRoot)
{
  if(PropScope::stack.size() > PropScope::MAX_DEPTH) {
    Utils::Logger::log("Prefab nesting too deep (possible self-reference); aborting expansion",
                       Utils::Logger::LEVEL_ERROR);
    return 0;
  }

  auto srcObj = &obj;
  bool isInstance = !savePrefabItself && obj.isPrefabInstance();
  if(isInstance)
  {
    auto prefab = ctx.project->getAssets().getPrefabByUUID(srcObj->uuidPrefab.value);
    if(prefab)srcObj = &prefab->obj;
  }

  // This node's override layer stays active for its whole subtree. Keys are path-precise
  // (combine(pathToTarget, propId)), so an override only resolves for its exact target. This
  // lets a compound prefab carry overrides into the prefabs it contains.
  PropScope::PrefabLayer objLayer{obj.propOverrides};

  uint16_t objFlags = 0;
  if(obj.enabled)objFlags |= P64::ObjectFlags::ACTIVE;
  if(!srcObj->children.empty() || !obj.children.empty())objFlags |= P64::ObjectFlags::HAS_CHILDREN;

  ctx.fileObj.write<uint16_t>(objFlags); // @TODO type
  ctx.fileObj.write<uint16_t>(runtimeId);
  ctx.fileObj.write<uint16_t>(parentRuntimeId);
  ctx.fileObj.write<uint8_t>(srcObj->visMask.resolve(obj.propOverrides));
  ctx.fileObj.write<uint8_t>(0); // padding

  glm::vec3 lpos   = srcObj->pos.resolve(obj.propOverrides);
  glm::vec3 lscale = srcObj->scale.resolve(obj.propOverrides);
  glm::quat lrot   = srcObj->rot.resolve(obj.propOverrides);

  if(isPrefabRoot) {
    lpos = {0,0,0};
    lscale = {1,1,1};
    lrot = glm::quat(glm::vec3(0.0f));
  }

  // World transform of this node. Used for what we write when expanding and as the
  // parent transform handed to children. At depth 0 parentTransform is identity, so
  // top-level and regular scene objects are written exactly as before.
  WorldTransform world{
    .pos   = parentTransform.pos + parentTransform.rot * (parentTransform.scale * lpos),
    .rot   = parentTransform.rot * lrot,
    .scale = parentTransform.scale * lscale
  };

  const glm::vec3 &wpos   = expanding ? world.pos   : lpos;
  const glm::vec3 &wscale = expanding ? world.scale : lscale;
  const glm::quat &wrot   = expanding ? world.rot   : lrot;

  ctx.fileObj.write(wpos);
  ctx.fileObj.write(wscale);
  uint32_t quatQuant = T3D::Quantizer::quatTo32Bit({wrot.x, wrot.y, wrot.z, wrot.w});
  ctx.fileObj.write(quatQuant);

  // DATA
  auto saveComp = [&ctx, &obj](Project::Component::Entry &comp) {
    auto compPos = ctx.fileObj.getPos();
    ctx.fileObj.skip(2);
    ctx.fileObj.skip(2); // flags (@TODO)

    if (Project::Component::isUsableId(comp.id)) {
      PropScope::Path compPath(comp.uuid); // resolve comp props relative to this object
      Project::Component::TABLE[comp.id].funcBuild(obj, comp, ctx);
    } else {
      Utils::Logger::log("Component ID not found: " + std::to_string(comp.id), Utils::Logger::LEVEL_ERROR);
      throw std::runtime_error("Object '" + obj.name + "': unknown component id " + std::to_string(comp.id));
    }

    ctx.fileObj.align(4);
    auto size = (ctx.fileObj.getPos() - compPos) / 4;
    // the record size is a u8 word count: a bigger record would corrupt the whole object stream,
    // so fail the build (nothing gets written) instead of truncating it
    if (size > 255) {
      throw std::runtime_error("Component '" + comp.name + "' (" + std::string(Project::Component::TABLE[comp.id].name)
        + ") on object '" + obj.name + "' is " + std::to_string(size * 4) + " bytes; the limit is 1020 bytes (255 words)");
    }

    ctx.fileObj.posPush(compPos);
    ctx.fileObj.write<uint8_t>(comp.id);
    ctx.fileObj.write<uint8_t>(size);
    ctx.fileObj.posPop();
    //ctx.fileObj.write<uint16_t>(comp.id);
  };


  std::vector<Project::Component::Entry*> compList{};
  for (auto &comp : srcObj->components) {
    PropScope::Path compPath(comp.uuid);
    if (!comp.enabled.resolve(obj)) continue;
    compList.push_back(&comp);
  }

  if(srcObj != &obj) {
    for (auto &comp : obj.components) {
      PropScope::Path compPath(comp.uuid);
      if (!comp.enabled.resolve(obj)) continue;
      compList.push_back(&comp);
    }
  }

  // Some components can only exist once per object (the runtime uses the first one, e.g. for
  // object references). Object::addComponent() refuses a second one, but a prefab can still pass
  // one on to an instance that has its own (an instance in a scene that was not loaded when it was
  // added to the prefab, or a hand-edited file): warn, older projects must keep building.
  const size_t prefabCompCount = srcObj != &obj ? srcObj->components.size() : 0;
  for (int singleId : {11, Project::Component::Spline::ID}) // rigidbody, spline
  {
    size_t count = 0;
    size_t fromPrefab = 0;
    for (size_t i = 0; i < compList.size(); ++i) {
      if (compList[i]->id != singleId)continue;
      ++count;
      if (i < prefabCompCount)++fromPrefab;
    }
    if (count > 1) {
      Utils::Logger::log("Object '" + obj.name + "' has " + std::to_string(count) + " "
        + std::string(Project::Component::TABLE[singleId].name) + " components"
        + (fromPrefab > 0 && fromPrefab < count ? " (from its prefab and its own)" : "")
        + ", but only one per object is supported: remove the extra one", Utils::Logger::LEVEL_WARN);
    }
  }

  // sort by component prio (unknown ids sort as 0, saveComp() then fails the build for them)
  auto getPrio = [](const Project::Component::Entry* comp) {
    bool known = Project::Component::isUsableId(comp->id);
    return known ? Project::Component::TABLE[comp->id].prio : 0;
  };
  std::stable_sort(compList.begin(), compList.end(),
    [&getPrio](const Project::Component::Entry* a, const Project::Component::Entry* b) {
      return getPrio(a) < getPrio(b);
    }
  );

  for(auto &comp : compList) {
    saveComp(*comp);
  }

  ctx.fileObj.write<uint32_t>(0);

  uint32_t count = 1;
  bool childExpanding = expanding || isInstance;
  for (const auto &child : srcObj->children) {
    PropScope::Path childPath(child->uuid); // this/outer layers address slots inside the child
    uint16_t childRuntimeId = childExpanding ? static_cast<uint16_t>(ctx.nextRuntimeId++)
                                             : child->runtimeId;
    count += writeObject(ctx, *child, savePrefabItself, childRuntimeId, runtimeId, childExpanding, world);
  }

  // For a prefab instance, also write children added directly to the instance in the
  // scene. These are real, world-positioned scene objects, not part of the prefab.
  // They resolve against their own overrides only, so the instance's cascade layer is
  // cleared first, otherwise their transform would resolve the instance's placement.
  if(isInstance && !obj.children.empty()) {
    PropScope::ResetScope freshScope; // resolve these against their own overrides only
    for (const auto &child : obj.children) {
      uint16_t childRuntimeId = expanding ? static_cast<uint16_t>(ctx.nextRuntimeId++)
                                          : child->runtimeId;
      count += writeObject(ctx, *child, savePrefabItself, childRuntimeId, runtimeId, expanding,
                           expanding ? world : WorldTransform{});
    }
  }
  return count;
}

void Build::buildScene(Project::Project &project, const Project::SceneEntry &scene, SceneCtx &ctx)
{
  std::string fileNameScene = "s" + Utils::padLeft(std::to_string(scene.id), '0', 4);
  std::string fileNameObj = fileNameScene + "o";

  std::unique_ptr<Project::Scene> sc{new Project::Scene(scene.id, project.getPath())};
  ctx.scene = sc.get();

  // Object ids only exist at runtime; assign them now so writeObject and component
  // builds (which resolve object UUID -> runtime id) see a consistent id space.
  // Expanded prefab-instance children get ids continuing past the scene objects.
  ctx.nextRuntimeId = sc->assignRuntimeIds();

  auto fsDataPath = fs::absolute(fs::path{project.getPath()} / "filesystem" / "p64");

  uint32_t sceneFlags = 0;
  uint32_t objCount = 0;

  if (sc->conf.doClearDepth.value)sceneFlags |= FLAG_CLR_DEPTH;
  if (sc->conf.doClearColor.value)sceneFlags |= FLAG_CLR_COLOR;
  if (sc->conf.fbFormat)sceneFlags |= FLAG_SCR_32BIT;

  ctx.fileObj = {};
  auto &rootObj = sc->getRootObject();
  for (const auto &child : rootObj.children) {
    objCount += writeObject(ctx, *child, false, child->runtimeId, 0, false);
  }

  ctx.fileObj.writeToFile(fsDataPath / fileNameObj);

  ctx.fileScene = {};
  ctx.fileScene.write<uint16_t>(sc->conf.fbWidth);
  ctx.fileScene.write<uint16_t>(sc->conf.fbHeight);
  ctx.fileScene.write(sceneFlags);
  ctx.fileScene.writeRGBA(sc->conf.clearColor.value);
  ctx.fileScene.write(objCount);

  ctx.fileScene.write<uint8_t>(sc->conf.renderPipeline.value);
  ctx.fileScene.write<uint8_t>(sc->conf.frameLimit.value);
  ctx.fileScene.write<uint8_t>(sc->conf.filter.value);
  ctx.fileScene.write<uint8_t>(0); // padding

  ctx.fileScene.write<uint16_t>(sc->conf.audioFreq.value);
  ctx.fileScene.write<uint16_t>(std::clamp(sc->conf.physicsTickRate.value, 1, 100));

  const auto &gravity = sc->conf.gravity.value;
  ctx.fileScene.write<float>(gravity.x);
  ctx.fileScene.write<float>(gravity.y);
  ctx.fileScene.write<float>(gravity.z);
  ctx.fileScene.write<float>(std::max(sc->conf.visualUnitsPerMeter.value, 0.001f));

  ctx.fileScene.write<uint8_t>(std::clamp(sc->conf.velocitySolverIterations.value, 1, 32));
  ctx.fileScene.write<uint8_t>(std::clamp(sc->conf.positionSolverIterations.value, 1, 32));
  ctx.fileScene.write<uint8_t>(sc->conf.interpolatePhysicsTransforms.value ? 1 : 0);
  ctx.fileScene.write<uint8_t>(0); // padding

  // Layer::Setup
  ctx.fileScene.write<uint8_t>(sc->conf.layers3D.size());
  ctx.fileScene.write<uint8_t>(sc->conf.layersPtx.size());
  ctx.fileScene.write<uint8_t>(sc->conf.layers2D.size());
  ctx.fileScene.write<uint8_t>(0); // padding

  auto writeLayer = [&ctx](const Project::LayerConf &layer) {
    uint32_t flags = 0;
    if(layer.depthWrite.value)flags |= (1 << 0);
    if(layer.depthCompare.value)flags |= (1 << 1);

    ctx.fileScene.write<uint32_t>(flags);
    ctx.fileScene.write<uint32_t>(layer.blender.value);

    uint8_t fogMode = layer.fog.value ? layer.fogColorMode.value : 0;
    ctx.fileScene.writeRGBA(layer.fogColor.value);
    ctx.fileScene.write<float>(layer.fogMin.value);
    ctx.fileScene.write<float>(layer.fogMax.value);
    ctx.fileScene.write<uint8_t>(fogMode);
    ctx.fileScene.write<uint8_t>(layer.lightMode.value);
    ctx.fileScene.write<uint8_t>(0); // padding
    ctx.fileScene.write<uint8_t>(0); // padding
  };

  for(const auto &layer : sc->conf.layers3D)writeLayer(layer);
  for(const auto &layer : sc->conf.layersPtx)writeLayer(layer);
  for(const auto &layer : sc->conf.layers2D)writeLayer(layer);

  ctx.fileScene.align(4);
  ctx.fileScene.writeToFile(fsDataPath / fileNameScene);

  ctx.files.push_back("filesystem/p64/" + fileNameScene);
  ctx.files.push_back("filesystem/p64/" + fileNameObj);

  ctx.scene = nullptr;
}
