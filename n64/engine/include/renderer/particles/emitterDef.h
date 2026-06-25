/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include <libdragon.h>

namespace P64::PTX
{
  // Material "set" flags in the baked .emit64 blob — must match emitter.cpp build().
  enum EmitterMatFlag : uint32_t
  {
    EMAT_BLENDER  = 1 << 0,
    EMAT_FOG      = 1 << 1,
    EMAT_ALPHACMP = 1 << 2,
    EMAT_FILTER   = 1 << 3,
    EMAT_PRIM     = 1 << 4,
    EMAT_ENV      = 1 << 5,
  };

  enum EmitterFireMode : uint8_t { EMIT_CONTINUOUS = 0, EMIT_ONE_SHOT = 1 };
  enum EmitterShape    : uint8_t { ESHAPE_POINT = 0, ESHAPE_SPHERE = 1, ESHAPE_BOX = 2, ESHAPE_CONE = 3 };
  enum EmitterAnimMode : uint8_t { EANIM_OFF = 0, EANIM_LOOP = 1, EANIM_OVER_LIFETIME = 2, EANIM_RANDOM = 3 };

  /**
   * ROM-loaded particle-emitter definition. This struct DIRECTLY OVERLAYS the .emit64
   * blob produced by Project::Assets::Emitter::build(): the N64 is big-endian and the
   * builder writes big-endian, so the bytes are already in native order. The struct is
   * `packed` and its field order/types MUST match build() exactly. Loaded once (not in a
   * hot path), so the unaligned packed accesses are fine.
   */
  struct __attribute__((packed)) EmitterDef
  {
    uint16_t magic;        // 0xE117
    uint16_t version;      // 1
    uint16_t maxParticles;
    uint8_t  fireMode;     // EmitterFireMode
    uint8_t  emitShape;    // EmitterShape
    float    spawnRate;    // particles/sec (continuous)
    uint16_t burstCount;   // particles per burst (one-shot)
    uint8_t  worldSpace;   // 0=relative to emitter, 1=baked into world at spawn
    uint8_t  texCount;     // number of entries in texAssetIdx[]

    float lifetime, lifetimeVariance;
    float shapeSize[3];
    float velocity[3];
    float speedMin, speedMax, spread;
    float gravity[3];
    float drag;
    float rotationMin, rotationMax, spinMin, spinMax;

    uint8_t colorStart[4]; // RGBA
    uint8_t colorEnd[4];
    float   sizeStart, sizeEnd;

    uint8_t texFrames;     // sprite-sheet sub-frames (>=1)
    uint8_t frameMode;     // EmitterAnimMode
    uint8_t swapMode;      // EmitterAnimMode (OFF/LOOP/OVER_LIFETIME)
    uint8_t mirrorAnim;
    float   frameRate, uvScrollSpeed, swapRate;

    uint32_t matFlags;     // EmitterMatFlag bitfield
    uint64_t cc;           // RDP color-combiner
    uint32_t blender;
    uint32_t fog;
    int32_t  alphaComp;
    int32_t  filter;
    uint32_t drawFlags;
    uint8_t  primColor[4];
    uint8_t  envColor[4];

    uint16_t texAssetIdx[]; // [texCount] — ROM asset indices (0xFFFF if missing)

    [[nodiscard]] bool hasMat(uint32_t flag) const { return (matFlags & flag) != 0; }
  };
}

// Global alias so user/component code can write AssetRef<emitter_t> (mirrors sprite_t/font64_t).
using emitter_t = P64::PTX::EmitterDef;
