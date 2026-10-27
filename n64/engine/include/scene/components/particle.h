/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include "assets/assetManager.h"
#include "scene/object.h"
#include "renderer/particles/emitterManager.h"

namespace P64::Comp
{
  // Scene component that places a particle emitter on an object. The component OWNS the
  // emitter (create()) so its lifetime is tied to the object, follows the object's position
  // each frame, and (optionally) plays on spawn.
  struct Particle
  {
    static constexpr uint32_t ID = 24; // fork component: reserved id block 24+
    static constexpr uint8_t FLAG_AUTO_PLAY = 1 << 0;
    // When set, deleting the object drops the emitter (and its live particles) immediately.
    // Default (unset): the emitter stops spawning but its live particles finish draining first.
    static constexpr uint8_t FLAG_FORCE_DESTROY = 1 << 1;

    AssetRef<emitter_t> emitter{};
    PTX::Handle handle{};
    uint8_t flags{0};
    // cached from the def at init: SPACE_PARENT emitters feed their parent's position each frame
    bool parentSpace{false};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) { return sizeof(Particle); }
    static void initDelete(Object& obj, Particle* data, uint16_t* initData);
    static void update(Object& obj, Particle* data, float deltaTime);
    // Non-auto-play emitters (re)play when the object receives EVENT_TYPE_PARTICLE_PLAY, letting
    // a ParticleSync coordinator start several emitters on one object together.
    static void onEvent(Object& obj, Particle* data, const ObjectEvent& event);
  };
}
