/**
* @copyright 2026 - Max Bebök
* @license MIT
*
* Shared, dependency-free particle simulation primitives.
*
* This header is the SINGLE SOURCE OF TRUTH for how particles spawn, integrate and
* animate. It is included by BOTH the desktop editor preview (which draws billboards
* into ImGui) and the N64 engine runtime (which writes into a tpx buffer).
*
* To guarantee the editor preview and the on-hardware result match, this code:
*  - carries its OWN seedable PRNG (never rand()/platform RNG, which differ),
*  - uses only plain `float` math + sqrt (sqrt is IEEE-correctly-rounded on both
*    platforms; sin/cos/tan/pow are NOT, so they are avoided in the spawn path),
*  - depends on NO libdragon / glm / engine types.
*
* Storage is owned by the caller (editor uses a std::vector, the engine uses a fixed
* pre-allocated array); this header only provides the math on plain structs.
*/
#pragma once
#include <cstdint>
#include <cmath>

namespace P64::ParticleSim
{
  struct Vec3 { float x{}, y{}, z{}; };

  inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
  inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
  inline Vec3 operator*(Vec3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }

  inline float dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
  inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
  inline Vec3 normalize(Vec3 a) {
    float l = length(a);
    return l > 1e-6f ? a * (1.0f / l) : Vec3{0,1,0};
  }
  inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

  // -- enums (mirror Project::Assets::Emitter / engine EmitterDef) --
  enum FireMode { FIRE_CONTINUOUS = 0, FIRE_ONE_SHOT = 1 };
  enum Shape    { SHAPE_POINT = 0, SHAPE_SPHERE = 1, SHAPE_BOX = 2, SHAPE_CONE = 3 };
  enum AnimMode { ANIM_OFF = 0, ANIM_LOOP = 1, ANIM_OVER_LIFETIME = 2, ANIM_RANDOM_STATIC = 3 };
  // What already-spawned particles are anchored to (what they move with):
  //  EMITTER: the emitter object (the whole system moves rigidly with it)
  //  WORLD:   nothing — baked into world at spawn
  //  PARENT:  the emitter object's PARENT — the emitter can move/orbit around the parent to
  //           spread particles, spawned particles stay put relative to the parent and follow it
  enum SimSpace { SPACE_EMITTER = 0, SPACE_WORLD = 1, SPACE_PARENT = 2 };

  // Deterministic, seedable xorshift32 PRNG (bit-stable across platforms).
  struct Rng
  {
    uint32_t s{1};

    void seed(uint32_t v) { s = v ? v : 1u; }

    uint32_t next()
    {
      s ^= s << 13;
      s ^= s >> 17;
      s ^= s << 5;
      return s;
    }

    // uniform float in [0,1)
    float nextF() { return (next() >> 8) * (1.0f / 16777216.0f); }
    // uniform float in [a,b)
    float range(float a, float b) { return a + (b - a) * nextF(); }
    // uniform float in [-1,1)
    float bipolar() { return nextF() * 2.0f - 1.0f; }
  };

  // Plain parameter set; both editor and engine build one of these from their model.
  struct Config
  {
    uint32_t maxParticles{64};
    float spawnRate{20.0f};       // particles/sec (continuous)
    int fireMode{FIRE_CONTINUOUS};
    uint32_t burstCount{16};      // particles per burst (one-shot)

    float lifetime{1.0f};
    float lifetimeVariance{0.0f}; // 0..1

    int emitShape{SHAPE_POINT};
    Vec3 shapeSize{};             // radius / half-extents
    Vec3 shapeOffset{};           // shape center relative to the emitter (ignored for POINT)

    Vec3 velocity{0, 1, 0};       // base direction
    float speedMin{20.0f};
    float speedMax{40.0f};
    float spread{0.0f};           // cone spread in degrees (0=tight)
    Vec3 gravity{0, -30, 0};
    float drag{0.0f};

    int simSpace{SPACE_EMITTER};  // SPACE_* — anchor of spawned particles (see SimSpace)
    // emitter yaw (precomputed sin/cos, default identity): rotates the emission shape and the
    // base velocity about Y so directional emitters (e.g. a wide wave) can face their movement.
    // Applied at SPAWN only; already-live particles keep their offsets.
    float yawSin{0.0f};
    float yawCos{1.0f};
    float rotationMin{0.0f};      // initial rotation range (deg)
    float rotationMax{0.0f};
    float spinMin{0.0f};          // per-particle angular velocity range (deg/sec)
    float spinMax{0.0f};

    float colorStart[4]{1, 1, 1, 1};
    float colorEnd[4]{1, 1, 1, 0};
    float sizeStart{1.0f};
    float sizeEnd{0.2f};

    uint32_t texFrames{1};        // sprite-sheet sub-frames (>=1)
    int frameMode{ANIM_OFF};
    float frameRate{8.0f};
    float uvScrollSpeed{0.0f};    // horizontal (U) scroll, sheet-frames/sec (1.0 = one frame width)
    bool mirrorAnim{false};       // mirrored playback: doubles the frames via tpx's mirror trick
    int swapMode{ANIM_OFF};
    float swapRate{4.0f};
    uint32_t swapCount{1};        // textures available to swap between (>=1)
  };

  // -- hardware limits (tpx / .emit64 constraints; previews must obey them too) --
  // Global size convention: at the camera-derived base size, a sim-size of 1.0 corresponds
  // to a tpx size byte of 64. Since the byte clamps at 127, sizes are NORMALIZED per emitter:
  // bytes span the full int8 range relative to the emitter's max size (sizeQuantMax) and the
  // tpx base-size is raised by the same factor at draw time (EmitterInstance::draw), so any
  // authored size range renders instead of clamping at ~2.0.
  constexpr float SIZE_TO_INT8 = 64.0f;
  // The per-particle tex offset is a u8 in 1/4px units of 8px sections -> 8 addressable
  // sections. Mirrored playback spends its logical frames in the same budget.
  constexpr uint32_t MAX_FRAME_SECTIONS = 8;

  // One live particle (local space). The engine keeps these in a parallel SoA;
  // the editor keeps a vector of them.
  struct Particle
  {
    Vec3 pos{};
    Vec3 vel{};
    float age{};
    float lifetime{1.0f};
    uint32_t seed{};   // per-particle seed (stable random frame, jitter)
    float angle0{};    // initial rotation (degrees)
    float spin{};      // angular velocity (deg/sec)
  };

  // Spawns a particle: picks an origin from the shape, a speed in [min,max] and a
  // direction within the spread cone, plus a per-particle seed. All randomness comes
  // from `rng`, so identical seeding yields identical sequences on editor + hardware.
  // `origin` is added to the spawn position (used for world-space emission so particles
  // are baked into world coordinates and no longer follow the emitter).
  inline Particle spawn(const Config &cfg, Rng &rng, Vec3 origin = {})
  {
    Particle p{};
    p.age = 0.0f;
    p.seed = rng.next();
    p.lifetime = cfg.lifetime * (1.0f - cfg.lifetimeVariance * rng.nextF());
    if(p.lifetime < 0.01f) p.lifetime = 0.01f;

    // origin within the emission shape (local space)
    switch(cfg.emitShape)
    {
      case SHAPE_SPHERE: {
        Vec3 d = normalize({rng.bipolar(), rng.bipolar(), rng.bipolar()});
        float r = cfg.shapeSize.x * std::sqrt(rng.nextF());
        p.pos = d * r;
      } break;
      case SHAPE_BOX:
        p.pos = {cfg.shapeSize.x * rng.bipolar(),
                 cfg.shapeSize.y * rng.bipolar(),
                 cfg.shapeSize.z * rng.bipolar()};
        break;
      case SHAPE_CONE: // emit from a disc at the base
        p.pos = {cfg.shapeSize.x * rng.bipolar(), 0.0f, cfg.shapeSize.x * rng.bipolar()};
        break;
      default: /* POINT */ p.pos = {0, 0, 0}; break;
    }
    // shape center offset (POINT stays on the emitter — move the object instead)
    if(cfg.emitShape != SHAPE_POINT) p.pos = p.pos + cfg.shapeOffset;

    // direction: base dir perturbed within the cone (trig-free, bit-stable)
    Vec3 base = normalize(cfg.velocity);
    float spreadAmt = cfg.spread / 90.0f; // 0=tight, 1=~hemisphere
    Vec3 jitter{rng.bipolar(), rng.bipolar(), rng.bipolar()};
    Vec3 dir = normalize(base + jitter * spreadAmt);

    float speed = rng.range(cfg.speedMin, cfg.speedMax);
    p.vel = dir * speed;

    // emitter yaw: rotate the shape offset and velocity about Y (identity when unset)
    p.pos = {p.pos.x * cfg.yawCos + p.pos.z * cfg.yawSin, p.pos.y,
             -p.pos.x * cfg.yawSin + p.pos.z * cfg.yawCos};
    p.vel = {p.vel.x * cfg.yawCos + p.vel.z * cfg.yawSin, p.vel.y,
             -p.vel.x * cfg.yawSin + p.vel.z * cfg.yawCos};

    p.pos = p.pos + origin;
    p.angle0 = rng.range(cfg.rotationMin, cfg.rotationMax);
    p.spin = rng.range(cfg.spinMin, cfg.spinMax);
    return p;
  }

  // Current rotation of a particle in degrees.
  inline float angleAt(const Particle &p) { return p.angle0 + p.spin * p.age; }

  // Advances one particle by dt. Returns false when the particle has expired.
  inline bool step(Particle &p, const Config &cfg, float dt)
  {
    p.vel = p.vel + cfg.gravity * dt;
    float damp = 1.0f - cfg.drag * dt;
    if(damp < 0.0f) damp = 0.0f;
    p.vel = p.vel * damp;
    p.pos = p.pos + p.vel * dt;
    p.age += dt;
    return p.age < p.lifetime;
  }

  // normalized life [0,1]
  inline float lifeT(const Particle &p) {
    return p.lifetime > 1e-6f ? (p.age / p.lifetime) : 1.0f;
  }

  inline void colorAt(const Config &cfg, float t, float out[4])
  {
    for(int i = 0; i < 4; ++i) out[i] = lerp(cfg.colorStart[i], cfg.colorEnd[i], t);
  }
  inline float sizeAt(const Config &cfg, float t) {
    return lerp(cfg.sizeStart, cfg.sizeEnd, t);
  }

  // The emitter's size-normalization factor: its max reachable size (sizes lerp start->end).
  inline float sizeQuantMax(const Config &cfg)
  {
    float m = cfg.sizeStart > cfg.sizeEnd ? cfg.sizeStart : cfg.sizeEnd;
    return m > 1e-4f ? m : 1.0f;
  }

  // Per-particle tpx size byte [1,127], normalized to the emitter's max size.
  inline int32_t hwSizeByte(const Config &cfg, float t)
  {
    float s = sizeAt(cfg, t) * (127.0f / sizeQuantMax(cfg));
    if(s < 1.0f) return 1;
    if(s > 127.0f) return 127;
    return (int32_t)s;
  }

  // The size the hardware actually shows: the quantized byte scaled back to sim units.
  // Previews use this so the on-device quantization/clamping is reproduced exactly.
  inline float hwSizeAt(const Config &cfg, float t)
  {
    return (float)hwSizeByte(cfg, t) * (sizeQuantMax(cfg) / 127.0f);
  }

  // Mirrored playback needs real frames to mirror (a mirrored single image renders doubled).
  inline bool mirrorActive(const Config &cfg) { return cfg.mirrorAnim && cfg.texFrames > 1; }

  // Frames the animation runs over: mirroring doubles them (the second half samples the
  // sheet mirrored on both axes in reverse order, handled by the tpx ucode / mirror sampler).
  inline uint32_t logicalFrameCount(const Config &cfg)
  {
    uint32_t n = cfg.texFrames < 1 ? 1 : cfg.texFrames;
    return mirrorActive(cfg) ? n * 2 : n;
  }

  // Current logical sprite-sheet frame [0, logicalFrameCount) for a particle.
  inline uint32_t frameForParticle(const Config &cfg, const Particle &p, [[maybe_unused]] float emitterTime)
  {
    uint32_t frames = logicalFrameCount(cfg);
    if(frames <= 1 || cfg.frameMode == ANIM_OFF) return 0;
    switch(cfg.frameMode)
    {
      case ANIM_OVER_LIFETIME: {
        uint32_t f = (uint32_t)(lifeT(p) * (float)frames);
        return f >= frames ? frames - 1 : f;
      }
      case ANIM_RANDOM_STATIC:
        return p.seed % frames;
      case ANIM_LOOP:
      default: {
        uint32_t f = (uint32_t)((p.age * cfg.frameRate));
        return f % frames;
      }
    }
  }

  // Current texture index [0, swapCount) for the whole emitter.
  inline uint32_t swapIndexForTime(const Config &cfg, float emitterTime)
  {
    if(cfg.swapCount <= 1 || cfg.swapMode == ANIM_OFF) return 0;
    if(cfg.swapMode == ANIM_LOOP) {
      return (uint32_t)(emitterTime * cfg.swapRate) % cfg.swapCount;
    }
    // OVER_LIFETIME of the emitter isn't meaningful for continuous; treat as loop.
    return (uint32_t)(emitterTime * cfg.swapRate) % cfg.swapCount;
  }

  // Accumulated horizontal (U-axis) UV scroll in SECTIONS (1 section = one sheet frame).
  // The engine converts sections to tpx 1/4px units (*32); previews divide by texFrames for
  // texture-normalized U. Wrapped to the repeat/mirror period so long-running emitters never
  // overflow the tpx int16 offset, and previews wrap identically.
  inline float uvScrollSections(const Config &cfg, float emitterTime)
  {
    float period = (float)logicalFrameCount(cfg);
    float o = cfg.uvScrollSpeed * emitterTime;
    o -= std::floor(o / period) * period;
    return o;
  }

  // Texture-normalized UV window of a logical frame incl. scroll, for the editor previews
  // (the engine passes section offsets to tpx directly). U is continuous in sheet units and
  // may leave [0,1]: sample with a REPEAT sampler, or MIRRORED-REPEAT when mirrorActive() —
  // that reproduces the RDP's infinite-repeat/mirror addressing exactly, including the
  // reversed frame order in the mirrored half. Frames in the mirrored half also flip V
  // (tpx's double-mirror = 180° rotated frames), expressed here as v in [1,2] so the
  // mirrored-repeat sampler performs the flip.
  inline void frameUV(const Config &cfg, uint32_t logicalFrame, float scrollSections,
                      float &u0, float &v0, float &u1, float &v1)
  {
    float n = (float)(cfg.texFrames < 1 ? 1 : cfg.texFrames);
    float s = (float)logicalFrame + scrollSections;
    u0 = s / n;
    u1 = (s + 1.0f) / n;
    v0 = 0.0f; v1 = 1.0f;
    if(mirrorActive(cfg)) {
      float m = std::fmod(s + 0.5f, 2.0f * n); // window center within the mirror cycle
      if(m >= n) { v0 = 1.0f; v1 = 2.0f; }     // mirrored half -> V flips via the sampler
    }
  }

  // Spawn scheduler: returns how many particles to emit this frame given remaining
  // capacity. `accum` carries the continuous fractional remainder between frames.
  // For one-shot, pass the per-emitter `firstFrame` flag and it bursts once.
  inline uint32_t spawnCount(const Config &cfg, float dt, float &accum, uint32_t freeSlots, bool &oneShotPending)
  {
    if(cfg.fireMode == FIRE_ONE_SHOT) {
      if(!oneShotPending) return 0;
      oneShotPending = false;
      uint32_t n = cfg.burstCount;
      return n > freeSlots ? freeSlots : n;
    }
    accum += cfg.spawnRate * dt;
    uint32_t n = (uint32_t)accum;
    accum -= (float)n;
    return n > freeSlots ? freeSlots : n;
  }
}
