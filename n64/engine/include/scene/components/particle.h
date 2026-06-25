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
    static constexpr uint32_t ID = 13;
    static constexpr uint8_t FLAG_AUTO_PLAY = 1 << 0;

    AssetRef<emitter_t> emitter{};
    PTX::Handle handle{};
    uint8_t flags{0};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData) { return sizeof(Particle); }
    static void initDelete(Object& obj, Particle* data, uint16_t* initData);
    static void update(Object& obj, Particle* data, float deltaTime);
  };
}
