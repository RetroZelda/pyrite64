/**
* @copyright 2025 - Max Bebök
* @license MIT
*/

#include "scene/components/collBody.h"

#include "scene/scene.h"
#include "scene/sceneManager.h"
#include <cmath>

namespace
{
  struct InitData
  {
    fm_vec3_t halfExtend{};
    fm_vec3_t offset{};
    uint8_t type{};
    uint8_t isTrigger{};
    uint8_t maskRead{};
    uint8_t maskWrite{};
    float friction{};
    float bounce{};
    color_t debugColor{};
  };
}

namespace P64::Comp
{
  void CollBody::initDelete([[maybe_unused]] Object& obj, CollBody* data, void* initData_)
  {
    InitData* initData = static_cast<InitData*>(initData_);
    auto &coll = SceneManager::getCurrent().getCollision();

    if (initData == nullptr) {
      if(data->registered)coll.removeCollider(&data->collider);
      data->~CollBody();
      return;
    }

    new(data) CollBody();

    data->collider.setOwner(&obj);
    data->collider.setFriction(initData->friction);
    data->collider.setBounce(initData->bounce);
    data->collider.setParentOffset(initData->offset);
    data->collider.setTrigger(initData->isTrigger);
    data->collider.setCollisionMask(initData->maskRead, initData->maskWrite);
    data->collider.setDebugColor(initData->debugColor);
    data->collider.setShapeType(static_cast<P64::Coll::ShapeType>(initData->type));
    data->collider.setHalfExtend(initData->halfExtend);
    // added to the collision scene by onEnable, which also runs for objects enabled at load
  }

  void CollBody::onEnable(Object& obj, CollBody* data)
  {
    // also called when only the visibility of an enabled object changes: the collision scene
    // must not get the same collider twice (it would also leak a broadphase node)
    if(data->registered)return;
    data->registered = true;
    obj.getScene().getCollision().addCollider(&data->collider);
  }

  void CollBody::onDisable(Object& obj, CollBody* data)
  {
    if(!data->registered)return;
    data->registered = false;
    obj.getScene().getCollision().removeCollider(&data->collider);
  }

}
