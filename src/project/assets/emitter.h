/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include "json.hpp"
#include "../../utils/prop.h"
#include "material.h"

namespace Build
{
  struct SceneCtx;
}

namespace Project::Assets
{
  /**
   * Editor-side data model for a particle-emitter asset (.emitter).
   *
   * Stores all emitter settings (emission, lifetime, shape, motion, the per-particle
   * start->end color/size pairs and the texture-animation config) plus an embedded
   * Material describing the combiner/blender/filter/etc. used to draw the particles.
   *
   * The primary texture is `material.tex0` (its tile/scale/mirror settings apply to
   * whichever texture is bound at runtime). `swapTextures` holds additional textures
   * the emitter can flip between over time ("full texture swaps"); the full swap
   * sequence is { material.tex0.texUUID } ++ swapTextures.
   *
   * The UUID lives inside the .emitter JSON (like Canvas), but unlike Canvas this asset
   * DOES produce a ROM binary (.emit64) and is added to the ROM asset table.
   */
  struct Emitter
  {
    // -- fire mode --
    static constexpr int FIRE_CONTINUOUS = 0;
    static constexpr int FIRE_ONE_SHOT   = 1;

    // -- emission shape --
    static constexpr int SHAPE_POINT  = 0;
    static constexpr int SHAPE_SPHERE = 1;
    static constexpr int SHAPE_BOX    = 2;
    static constexpr int SHAPE_CONE   = 3;

    // -- texture-frame / swap animation modes --
    static constexpr int ANIM_OFF           = 0;
    static constexpr int ANIM_LOOP          = 1;
    static constexpr int ANIM_OVER_LIFETIME = 2;
    static constexpr int ANIM_RANDOM_STATIC = 3; // (frames only)

    std::string name{};
    std::string path{};
    uint64_t uuid{0};

    // -- emission --
    PROP_U32(maxParticles);       // pre-allocated buffer capacity
    PROP_FLOAT(spawnRate);        // particles / second (continuous)
    PROP_S32(fireMode);           // FIRE_*
    PROP_U32(burstCount);         // particles emitted at once (one-shot)

    // -- lifetime --
    PROP_FLOAT(lifetime);         // seconds
    PROP_FLOAT(lifetimeVariance); // 0..1 fraction of randomness

    // -- emission shape --
    PROP_S32(emitShape);          // SHAPE_*
    PROP_VEC3(shapeSize);         // radius / half-extents / cone params

    // -- initial motion / forces --
    PROP_VEC3(velocity);          // base direction (normalized-ish)
    PROP_FLOAT(speedMin);
    PROP_FLOAT(speedMax);
    PROP_FLOAT(spread);           // cone spread in degrees
    PROP_VEC3(gravity);
    PROP_FLOAT(drag);             // per-second velocity damping

    // -- simulation space --
    PROP_BOOL(worldSpace);        // false: particles relative to emitter; true: placed in world at spawn

    // -- per-particle rotation --
    PROP_FLOAT(rotationMin);      // initial rotation range (deg)
    PROP_FLOAT(rotationMax);
    PROP_FLOAT(spinMin);          // angular velocity range (deg/sec)
    PROP_FLOAT(spinMax);

    // -- per-particle start -> end (the only tpx-animatable channels) --
    PROP_VEC4(colorStart);        // RGBA 0..1
    PROP_VEC4(colorEnd);
    PROP_FLOAT(sizeStart);
    PROP_FLOAT(sizeEnd);

    // -- texture --
    Material material{};          // combiner/blender/filter/...; tex0 is the primary texture
    std::vector<uint64_t> swapTextures{}; // extra textures for swap animation (UUIDs)

    // -- texture animation --
    PROP_U32(texFrames);          // sprite-sheet sub-frames within a texture (>=1)
    PROP_S32(frameMode);          // ANIM_*
    PROP_FLOAT(frameRate);        // frames / second (ANIM_LOOP)
    PROP_FLOAT(uvScrollSpeed);    // horizontal (U-axis) scroll, units / second
    PROP_S32(swapMode);           // ANIM_* (OFF/LOOP/OVER_LIFETIME)
    PROP_FLOAT(swapRate);         // textures / second
    PROP_BOOL(mirrorAnim);        // sprite-sheet mirror trick (halves rotation frames)

    Emitter();

    // Full ordered texture sequence: primary (material.tex0) followed by swapTextures,
    // skipping any unset (0) entries.
    [[nodiscard]] std::vector<uint64_t> textureSequence() const;

    [[nodiscard]] nlohmann::json serialize() const;
    void deserialize(const nlohmann::json &doc);

    // Bakes the compact .emit64 runtime blob (consumed by the engine's P64::PTX::EmitterDef).
    // The exact byte layout is documented in emitter.cpp and MUST match emitterDef.h.
    void build(Utils::BinaryFile &file, Build::SceneCtx &sceneCtx) const;

    void load(const std::string &filePath);
    void save(const std::string &filePath) const;
    void save() const { save(path); }

    bool operator==(const Emitter &o) const = default;
  };
}
