/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include <libdragon.h>

#include "scene/sceneManager.h"

#include "scene/scene.h"
#include "script/globalScript.h"
#include "vi/swapChain.h"
#include "events/bus.h"

namespace {
  constinit P64::Scene* currScene{nullptr};
  constinit uint32_t sceneId{0};
  constinit uint32_t nextSceneId{0};
  constinit uint8_t forceReload{0};
}

// "Private" methods only used in main.cpp
namespace P64::SceneManager
{
  void load(uint16_t newSceneId) {
    nextSceneId = newSceneId;
  }

  void reload() {
    nextSceneId = sceneId;
    forceReload = 1;
  }

  Scene& getCurrent() {
    return *currScene;
  }

  void run()
  {
    GlobalScript::callHooks(GlobalScript::HookType::SCENE_PRE_LOAD);
    // Fork: event bus. Immediate: no frame runs before the scene exists.
    Events::Internal::postImmediate(Events::Builtin::SceneLoading, nextSceneId);

    sceneId = nextSceneId;
    currScene = new P64::Scene(sceneId, &currScene);

    GlobalScript::callHooks(GlobalScript::HookType::SCENE_POST_LOAD);
    // Fork: event bus. Queued: delivered in the first frame, after every onReady.
    Events::post(Events::Builtin::SceneLoaded, sceneId);
    Events::post(Events::Builtin::CurrentSceneLoaded, sceneId);

    while(sceneId == nextSceneId && !forceReload) {
      currScene->update(VI::SwapChain::getDeltaTime());
    }
    forceReload = 0;
  }

  void unload()
  {
    GlobalScript::callHooks(GlobalScript::HookType::SCENE_PRE_UNLOAD);
    // Fork: event bus. Immediate: the scene is gone before another frame runs.
    Events::Internal::postImmediate(Events::Builtin::SceneUnloading, sceneId);
    Events::Internal::postImmediate(Events::Builtin::CurrentSceneUnloading, sceneId);
    delete currScene;
    Events::Internal::onSceneDestroyed();
    Events::Internal::postImmediate(Events::Builtin::SceneUnloaded, sceneId);
    GlobalScript::callHooks(GlobalScript::HookType::SCENE_POST_UNLOAD);
    currScene = nullptr;
  }
}