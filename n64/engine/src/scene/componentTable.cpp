/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "scene/componentTable.h"
#include "scene/scene.h"
#include <iterator>
#include <type_traits>

#include "scene/components/code.h"
#include "scene/components/model.h"
#include "scene/components/light.h"
#include "scene/components/camera.h"
#include "scene/components/collMesh.h"
#include "scene/components/collBody.h"
#include "scene/components/rigidBody.h"
#include "scene/components/audio2d.h"
#include "scene/components/constraint.h"
#include "scene/components/culling.h"
#include "scene/components/nodeGraph.h"
#include "scene/components/animModel.h"
#include "scene/components/charBody.h"
#include "scene/components/surface.h"
#include "scene/components/particle.h"
#include "scene/components/spline.h"

// some template magic to auto-detect if a function exists in a component
#define HAS_FUNC_TPL(NAME_HAS, NAME_GET, FUNC) \
  template<typename T, typename = void> \
  struct NAME_HAS : std::false_type {}; \
  \
  template<typename T> \
  struct NAME_HAS<T, std::void_t<decltype(&T::FUNC)>> : std::true_type {}; \
  \
  template<typename T> \
  auto NAME_GET() { \
    if constexpr (NAME_HAS<T>::value) { return &T::FUNC; } else { return nullptr; } \
  }

namespace
{
  HAS_FUNC_TPL(has_enable, get_on_enable,  onEnable  )
  HAS_FUNC_TPL(has_disable,get_on_disable, onDisable )
  HAS_FUNC_TPL(has_ready,get_on_ready, onReady )
  HAS_FUNC_TPL(has_draw,   get_draw,    draw   )
  HAS_FUNC_TPL(has_update, get_update,  update )
  HAS_FUNC_TPL(has_fixed_update, get_fixed_update, fixedUpdate)
  HAS_FUNC_TPL(has_event,  get_event,   onEvent)
  HAS_FUNC_TPL(has_coll,   get_coll,    onColl )
}

#define SET_COMP(NAME) \
  [Comp::NAME::ID] = { \
    .initDel = reinterpret_cast<FuncInitDel>(Comp::NAME::initDelete), \
    .onEnable = (FuncEnabled)get_on_enable<Comp::NAME>(), \
    .onDisable = (FuncDisabled)get_on_disable<Comp::NAME>(), \
    .onReady = (FuncReady)get_on_ready<Comp::NAME>(), \
    .update = (FuncUpdate)get_update<Comp::NAME>(), \
    .fixedUpdate = (FuncFixedUpdate)get_fixed_update<Comp::NAME>(), \
    .draw   = (FuncDraw)(get_draw<Comp::NAME>()), \
    .onEvent = (FuncOnEvent)(get_event<Comp::NAME>()), \
    .onColl = (FuncOnColl)(get_coll<Comp::NAME>()), \
    .getAllocSize = reinterpret_cast<FuncGetAllocSize>(Comp::NAME::getAllocSize), \
  }

namespace P64
{
  const ComponentDef COMP_TABLE[COMP_TABLE_SIZE] {
    SET_COMP(Code),
    SET_COMP(Model),
    SET_COMP(Light),
    SET_COMP(Camera),
    SET_COMP(CollMesh),
    SET_COMP(CollBody),
    SET_COMP(Audio2D),
    SET_COMP(Constraint),
    SET_COMP(Culling),
    SET_COMP(NodeGraph),
    SET_COMP(AnimModel),
    SET_COMP(RigidBody),
    SET_COMP(CharBody),
    SET_COMP(Surface),
    // unused ids up to the fork block: GCC only supports designators without gaps
    [14] = {}, [15] = {}, [16] = {}, [17] = {}, [18] = {},
    [19] = {}, [20] = {}, [21] = {}, [22] = {}, [23] = {},
    // fork components, reserved id block 24+ (keep the designators ascending)
    SET_COMP(Particle),
    SET_COMP(Spline),
  };

  static_assert(P64::Comp::Surface::ID == 13 && P64::Comp::Particle::ID == 24, "update the empty designators above");
  static_assert(P64::Comp::Spline::ID < P64::COMP_TABLE_SIZE);

  // Ids are stored in scene and prefab data, so two components must never share one
  constexpr uint32_t ALL_COMP_IDS[] {
    Comp::Code::ID, Comp::Model::ID, Comp::Light::ID, Comp::Camera::ID, Comp::CollMesh::ID,
    Comp::CollBody::ID, Comp::Audio2D::ID, Comp::Constraint::ID, Comp::Culling::ID,
    Comp::NodeGraph::ID, Comp::AnimModel::ID, Comp::RigidBody::ID, Comp::CharBody::ID,
    Comp::Surface::ID, Comp::Particle::ID, Comp::Spline::ID,
  };
  static_assert([]{
    for(uint32_t i = 0; i < std::size(ALL_COMP_IDS); ++i) {
      for(uint32_t j = i + 1; j < std::size(ALL_COMP_IDS); ++j) {
        if(ALL_COMP_IDS[i] == ALL_COMP_IDS[j])return false;
      }
    }
    return true;
  }(), "component ids must be unique");
}
