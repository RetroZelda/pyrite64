/**
 * @file rigidBody.cpp
 * @author Kevin Reier <https://github.com/Byterset>
 * @brief Defines the RigidBody component, which allows an Object to have a rigid body for physics simulation (see rigidBody.h)
 */
#include "scene/object.h"
#include "scene/components/rigidBody.h"
#include "lib/logger.h"
#include "scene/scene.h"
#include "scene/sceneManager.h"
#include <cmath>

namespace
{
  struct InitData
  {
    float mass{};
    bool isKinematic{};
    bool constrainPosX{};
    bool constrainPosY{};
    bool constrainPosZ{};
    bool constrainRotX{};
    bool constrainRotY{};
    bool constrainRotZ{};
    bool hasGravity{};
    float gravityScalar{};
    float timeScalar{};
    float angularDamping{};
  };
}

namespace P64::Comp
{
  void RigidBody::initDelete([[maybe_unused]] Object& obj, RigidBody* data, void* initData_)
  {
    InitData* initData = static_cast<InitData*>(initData_);
    auto &coll = SceneManager::getCurrent().getCollision();

    if (initData == nullptr) {
      if(data->registered)coll.removeRigidBody(&data->rigidBody);
      // TODO: add collider to new collision scene
      data->~RigidBody();
      return;
    }

    //existing rigidBodies for this object in the scene? If yes don't add another one and log an error
    auto existing = coll.findRigidBodyByObjectId(obj.id);
    if(existing) {
      P64::Log::error("Object '%d' already has a Rigidbody component, cannot add another one", obj.id);
      return;
    }

    new (data) RigidBody();

    data->rigidBody.init(&obj, initData->mass);
    data->rigidBody.setKinematic(initData->isKinematic);
    data->rigidBody.setHasGravity(initData->hasGravity);
    data->rigidBody.setGravityScale(initData->gravityScalar);
    data->rigidBody.setTimeScale(initData->timeScalar);
    data->rigidBody.setAngularDamping(initData->angularDamping);
    Coll::Constraint constraints = Coll::Constraint::None;
    if(initData->constrainPosX) constraints = constraints | Coll::Constraint::FreezePosX;
    if(initData->constrainPosY) constraints = constraints | Coll::Constraint::FreezePosY;
    if(initData->constrainPosZ) constraints = constraints | Coll::Constraint::FreezePosZ;
    if(initData->constrainRotX) constraints = constraints | Coll::Constraint::FreezeRotX;
    if(initData->constrainRotY) constraints = constraints | Coll::Constraint::FreezeRotY;
    if(initData->constrainRotZ) constraints = constraints | Coll::Constraint::FreezeRotZ;
    data->rigidBody.setConstraints(constraints);
    // added to the collision scene in onEnable (runs right after this for an enabled object).
    // Adding it here as well registered every body twice, so it was integrated twice per step.
  }

  void RigidBody::onEnable(Object& obj, RigidBody* data)
  {
    // also called when only the visibility of an enabled object changes: register once
    if(data->registered)return;
    auto &coll = obj.getScene().getCollision();
    if(coll.findRigidBodyByObjectId(obj.id)) {
      P64::Log::error("Object '%d' already has a Rigidbody component, cannot add another one", obj.id);
      return;
    }
    data->registered = true;
    coll.addRigidBody(&data->rigidBody);
    coll.enableRigidBody(&data->rigidBody); // removeRigidBody() disabled it when the object was disabled
  }

  void RigidBody::onDisable(Object& obj, RigidBody* data)
  {
    if(!data->registered)return;
    data->registered = false;
    obj.getScene().getCollision().removeRigidBody(&data->rigidBody);
  }

  void RigidBody::update(Object &obj, RigidBody* data, float deltaTime)
  {
  }
}
