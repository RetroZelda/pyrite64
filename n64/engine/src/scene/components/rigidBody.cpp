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
      coll.removeRigidBody(&data->rigid_body);
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

    data->rigid_body = Coll::RigidBody{};
    data->rigid_body.init(&obj, initData->mass);
    data->rigid_body.setKinematic(initData->isKinematic);
    data->rigid_body.setHasGravity(initData->hasGravity);
    data->rigid_body.setGravityScale(initData->gravityScalar);
    data->rigid_body.setTimeScale(initData->timeScalar);
    data->rigid_body.setAngularDamping(initData->angularDamping);
    Coll::Constraint constraints = Coll::Constraint::None;
    if(initData->constrainPosX) constraints = constraints | Coll::Constraint::FreezePosX;
    if(initData->constrainPosY) constraints = constraints | Coll::Constraint::FreezePosY;
    if(initData->constrainPosZ) constraints = constraints | Coll::Constraint::FreezePosZ;
    if(initData->constrainRotX) constraints = constraints | Coll::Constraint::FreezeRotX;
    if(initData->constrainRotY) constraints = constraints | Coll::Constraint::FreezeRotY;
    if(initData->constrainRotZ) constraints = constraints | Coll::Constraint::FreezeRotZ;
    data->rigid_body.setConstraints(constraints);
  }

  void RigidBody::onEnable(Object& obj, RigidBody* data)
  {
    return obj.getScene().getCollision().addRigidBody(&data->rigid_body);
  }

  void RigidBody::onDisable(Object& obj, RigidBody* data)
  {
    return obj.getScene().getCollision().removeRigidBody(&data->rigid_body);
  }

  void RigidBody::update(Object &obj, RigidBody* data, float deltaTime)
  {
  }
}
