/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "renderer/particles/emitterInstance.h"
#include "assets/assetManager.h"

#include <libdragon.h>
#include <cstdlib>
#include <algorithm>

namespace
{
  // Tuning constants (visual scale on hardware — adjust to taste).
  // SIZE_TO_INT8 maps the sim size to the tpx int8 size byte (size 1.0 -> byte 64). The
  // on-screen scale is governed mostly by base_size, which drawPtx() now sets from the camera
  // far plane (camera-correct, matching the editor preview's SIZE_TO_WORLD=8.0 world billboards).
  // If the on-device size still differs from the preview, nudge this single constant.
  // Note the int8 [1,127] clamp: size 1.0 -> 64, so sizes above ~1.98 clamp.
  constexpr float SIZE_TO_INT8 = 64.0f;  // particle size 1.0 -> int8 ~64
  constexpr float FRAME_STRIDE = 32.0f;  // one 8px sprite-sheet frame in tpx tex-offset units

  namespace PS = P64::ParticleSim;

  PS::Config configFromDef(const P64::PTX::EmitterDef* d)
  {
    PS::Config c{};
    c.maxParticles = d->maxParticles;
    c.spawnRate = d->spawnRate;
    c.fireMode = d->fireMode;
    c.burstCount = d->burstCount;
    c.lifetime = d->lifetime;
    c.lifetimeVariance = d->lifetimeVariance;
    c.emitShape = d->emitShape;
    c.shapeSize = {d->shapeSize[0], d->shapeSize[1], d->shapeSize[2]};
    c.velocity = {d->velocity[0], d->velocity[1], d->velocity[2]};
    c.speedMin = d->speedMin;
    c.speedMax = d->speedMax;
    c.spread = d->spread;
    c.gravity = {d->gravity[0], d->gravity[1], d->gravity[2]};
    c.drag = d->drag;
    c.worldSpace = d->worldSpace != 0;
    c.rotationMin = d->rotationMin;
    c.rotationMax = d->rotationMax;
    c.spinMin = d->spinMin;
    c.spinMax = d->spinMax;
    for(int i = 0; i < 4; ++i) {
      c.colorStart[i] = d->colorStart[i] / 255.0f;
      c.colorEnd[i] = d->colorEnd[i] / 255.0f;
    }
    c.sizeStart = d->sizeStart;
    c.sizeEnd = d->sizeEnd;
    c.texFrames = d->texFrames < 1 ? 1 : d->texFrames;
    c.frameMode = d->frameMode;
    c.frameRate = d->frameRate;
    c.uvScrollSpeed = d->uvScrollSpeed;
    c.swapMode = d->swapMode;
    c.swapRate = d->swapRate;
    c.swapCount = d->texCount < 1 ? 1 : d->texCount;
    return c;
  }

  void uploadTexTile(sprite_t* spr, bool mirror)
  {
    if(!spr) return;
    // tpx maps the texture to an 8x8 base per particle; scale_log fits the real size to that.
    // Derive it PER AXIS from the actual width/height — using height for both axes tiles a
    // non-square texture (e.g. a 32x64) on the mismatched axis and renders the image doubled.
    int shiftS = -__builtin_ctz(spr->width  / 8);
    int shiftT = -__builtin_ctz(spr->height / 8);
    rdpq_texparms_t p{};
    p.s = {.translate = 0, .scale_log = shiftS, .repeats = REPEAT_INFINITE, .mirror = mirror};
    p.t = {.translate = 0, .scale_log = shiftT, .repeats = REPEAT_INFINITE, .mirror = mirror};
    rdpq_sprite_upload(TILE0, spr, &p);
  }
}

void P64::PTX::EmitterInstance::buildSetupBlock()
{
  rspq_block_begin();
  {
    rdpq_mode_begin();
      // combiner: per-particle color is fed as PRIM; default modulates texture by it.
      rdpq_combiner_t comb = def->cc
        ? (rdpq_combiner_t)def->cc
        : RDPQ_COMBINER1((PRIM,0,TEX0,0), (TEX0,0,PRIM,0));
      rdpq_mode_combiner(comb);

      rdpq_mode_filter(def->hasMat(EMAT_FILTER) ? (rdpq_filter_t)def->filter : FILTER_POINT);
      rdpq_mode_alphacompare(def->hasMat(EMAT_ALPHACMP) ? def->alphaComp : 10);
      if(def->hasMat(EMAT_BLENDER)) rdpq_mode_blender(def->blender);
      if(def->hasMat(EMAT_FOG))     rdpq_mode_fog(def->fog);
      rdpq_mode_zbuf(true, false); // depth-test, no depth-write (particles)
    rdpq_mode_end();

    if(def->hasMat(EMAT_PRIM)) {
      rdpq_set_prim_color((color_t){def->primColor[0], def->primColor[1], def->primColor[2], def->primColor[3]});
    }
    if(def->hasMat(EMAT_ENV)) {
      rdpq_set_env_color((color_t){def->envColor[0], def->envColor[1], def->envColor[2], def->envColor[3]});
    }

    // Single-texture emitters upload here (once). Swap emitters upload per-frame in draw().
    if(def->texCount <= 1 && texSprites && texSprites[0]) {
      uploadTexTile(texSprites[0], def->mirrorAnim != 0);
    }
    tpx_state_set_scale(1.0f, 1.0f);
  }
  setupDPL = rspq_block_end();
}

void P64::PTX::EmitterInstance::init(const EmitterDef* d, uint32_t seed)
{
  def = d;
  cfg = configFromDef(d);
  uint32_t maxP = cfg.maxParticles < 2 ? 2 : cfg.maxParticles;

  system = new System(System::TEX_A_S16, maxP);
  parts = (ParticleSim::Particle*)malloc(sizeof(ParticleSim::Particle) * maxP);
  drawOrder = (uint16_t*)malloc(sizeof(uint16_t) * maxP);

  // preload swap textures
  if(d->texCount > 0) {
    texSprites = (sprite_t**)malloc(sizeof(sprite_t*) * d->texCount);
    for(uint8_t i = 0; i < d->texCount; ++i) {
      texSprites[i] = (d->texAssetIdx[i] != 0xFFFF)
        ? (sprite_t*)AssetManager::getByIndex(d->texAssetIdx[i]) : nullptr;
    }
  }

  mat = (T3DMat4FP*)malloc_uncached(sizeof(T3DMat4FP));
  system->mat = mat;

  buildSetupBlock();

  rng.seed(seed ? seed : 0x9E3779B9u);
  reset();
}

void P64::PTX::EmitterInstance::teardown()
{
  // The RSP/RDP may still be reading our buffers/block from a prior triple-buffered frame.
  rspq_wait();
  if(setupDPL) { rspq_block_free(setupDPL); setupDPL = nullptr; }
  if(system) { delete system; system = nullptr; }   // frees the uncached tpx buffer
  if(parts) { free(parts); parts = nullptr; }
  if(drawOrder) { free(drawOrder); drawOrder = nullptr; }
  if(texSprites) { free(texSprites); texSprites = nullptr; }
  if(mat) { free_uncached(mat); mat = nullptr; }
  def = nullptr;
}

void P64::PTX::EmitterInstance::reset()
{
  spawnAccum = 0.0f;
  emitterTime = 0.0f;
  oneShotPending = true;
  lastSwapIdx = 0xFFFF;
  if(system) system->count = 0;
}

bool P64::PTX::EmitterInstance::finished() const
{
  return cfg.fireMode == ParticleSim::FIRE_ONE_SHOT && system && system->count == 0 && !oneShotPending;
}

void P64::PTX::EmitterInstance::update(float dt, const fm_vec3_t &camPos, bool haveCam)
{
  if(!system || !parts) return;
  emitterTime += dt;

  // spawn
  uint32_t freeSlots = cfg.maxParticles > system->count ? cfg.maxParticles - system->count : 0;
  uint32_t n = ParticleSim::spawnCount(cfg, dt, spawnAccum, freeSlots, oneShotPending);
  ParticleSim::Vec3 origin = cfg.worldSpace ? ParticleSim::Vec3{pos.x, pos.y, pos.z} : ParticleSim::Vec3{};
  for(uint32_t i = 0; i < n; ++i) {
    parts[system->count] = ParticleSim::spawn(cfg, rng, origin);
    ++system->count;
  }

  // integrate + cull (swap-pop the cached SoA; the uncached buffer is rewritten below)
  for(uint32_t i = 0; i < system->count; ) {
    if(!ParticleSim::step(parts[i], cfg, dt)) {
      parts[i] = parts[--system->count];
    } else {
      ++i;
    }
  }

  // Back-to-front sort so overlapping transparent particles layer correctly (matches the
  // editor preview, which sorts by depth). We sort a lightweight uint16 index array, not the
  // 44-byte Particle structs. Particles are stored in LOCAL space, so transform the camera
  // into that space once (a constant offset) instead of every particle into world space.
  const bool doSort = haveCam && system->count > 1;
  if(doSort) {
    ParticleSim::Vec3 camLocal = cfg.worldSpace
      ? ParticleSim::Vec3{camPos.x, camPos.y, camPos.z}
      : ParticleSim::Vec3{camPos.x - pos.x, camPos.y - pos.y, camPos.z - pos.z};
    for(uint32_t i = 0; i < system->count; ++i) drawOrder[i] = (uint16_t)i;
    std::sort(drawOrder, drawOrder + system->count, [&](uint16_t a, uint16_t b) {
      ParticleSim::Vec3 da = parts[a].pos - camLocal;
      ParticleSim::Vec3 db = parts[b].pos - camLocal;
      return ParticleSim::dot(da, da) > ParticleSim::dot(db, db); // farthest first
    });
  }

  // write alive particles into the uncached tpx buffer (write-only), farthest first
  auto buff = system->getBufferS16();
  for(uint32_t i = 0; i < system->count; ++i) {
    auto &p = parts[doSort ? drawOrder[i] : i];
    float t = ParticleSim::lifeT(p);

    int16_t* pp = tpx_buffer_s16_get_pos(buff, i);
    pp[0] = (int16_t)p.pos.x;
    pp[1] = (int16_t)p.pos.y;
    pp[2] = (int16_t)p.pos.z;

    float sz = ParticleSim::sizeAt(cfg, t) * SIZE_TO_INT8;
    if(sz < 1.0f) sz = 1.0f; else if(sz > 127.0f) sz = 127.0f;
    *tpx_buffer_s16_get_size(buff, i) = (int8_t)sz;

    float col[4];
    ParticleSim::colorAt(cfg, t, col);
    uint8_t* rgba = tpx_buffer_s16_get_rgba(buff, i);
    rgba[0] = (uint8_t)(col[0] * 255.0f);
    rgba[1] = (uint8_t)(col[1] * 255.0f);
    rgba[2] = (uint8_t)(col[2] * 255.0f);
    rgba[3] = (uint8_t)(col[3] * 255.0f);

    uint32_t frame = ParticleSim::frameForParticle(cfg, p, emitterTime);
    *tpx_buffer_s16_get_tex_offset(buff, i) = (uint8_t)(frame * (uint32_t)FRAME_STRIDE);
  }

  if(cfg.fireMode == ParticleSim::FIRE_ONE_SHOT && system->count == 0 && !oneShotPending) {
    // drained — handled by the manager (reclaim/stop)
  }

  // refresh the transform matrix: relative emitters follow `pos`, world-space emitters are baked.
  if(mat) {
    if(cfg.worldSpace) {
      t3d_mat4fp_identity(mat);
    } else {
      t3d_mat4fp_from_srt_euler(mat,
        (float[]){1.0f, 1.0f, 1.0f}, (float[]){0, 0, 0}, (float[]){pos.x, pos.y, pos.z});
    }
  }
}

void P64::PTX::EmitterInstance::draw()
{
  if(!system || system->count == 0) return;

  rspq_block_run(setupDPL);

  // swap emitters re-upload the current texture each frame (the ucode only uses TILE0)
  if(def->texCount > 1 && texSprites) {
    uint32_t si = ParticleSim::swapIndexForTime(cfg, emitterTime);
    if(si >= def->texCount) si = 0;
    uploadTexTile(texSprites[si], def->mirrorAnim != 0);
  }

  // global horizontal UV scroll + optional sprite-sheet mirror.
  // Mirroring only makes sense for an animated sprite-sheet (texFrames>1): the mirror point is a
  // half-frame boundary that halves the frame count. With a single image (texFrames==1) passing
  // mirrorPt=1 mirrors the texture across the particle midline -> the image renders DOUBLED, so
  // force no-mirror (0 -> tpx treats as 128 = none) unless there are real frames to mirror.
  int16_t scrollOff = (int16_t)(emitterTime * cfg.uvScrollSpeed * FRAME_STRIDE);
  uint16_t mirrorPt = (def->mirrorAnim && cfg.texFrames > 1) ? (uint16_t)cfg.texFrames : 0;
  tpx_state_set_tex_params(scrollOff, mirrorPt);

  system->draw(); // pushes/pops our transform matrix internally
}

uint32_t P64::PTX::EmitterInstance::reservedBytes() const
{
  if(!def) return 0;
  uint32_t maxP = cfg.maxParticles;
  uint32_t bytes = 0;
  if(system) bytes += ((maxP + 1u) & ~1u) * system->strideBytes(); // uncached tpx buffer
  bytes += maxP * (uint32_t)sizeof(ParticleSim::Particle);          // cached SoA
  bytes += maxP * (uint32_t)sizeof(uint16_t);                       // back-to-front index array
  bytes += (uint32_t)sizeof(T3DMat4FP);                             // matrix
  bytes += def->texCount * (uint32_t)sizeof(sprite_t*);             // swap-texture pointers
  return bytes;
}
