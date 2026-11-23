/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph (docs/todo/event-graph/PLAN.md, section 4.6). Turns the collision scene's
// per-step contacts into enter and exit: the Collider / Collision-Mesh pins Enter, Exit, Occupied
// and Emptied, and the script callbacks onCollisionEnter / onCollisionExit.
#pragma once
#include <cstdint>

namespace P64 { class Object; }
namespace P64::Coll { class Collider; class MeshCollider; }

namespace P64::Events::Contacts
{
  /** Called by the collision scene before it reports this step's contacts. */
  void beginStep();

  /**
   * 'self' touches 'other' this step, through its collider or mesh collider. Only objects with
   * ObjectFlags::WANTS_CONTACTS are tracked.
   */
  void touch(Coll::Collider* selfCollider, Coll::MeshCollider* selfMesh, Object& self, Object& other, bool trigger);

  /** Called after all contacts of the step: reports what started and what ended. */
  void endStep();

  /** A collider left the collision scene: forget its contacts without reporting them. */
  void forget(const void* collider);

  /** Forget everything (scene teardown). */
  void reset();
}
