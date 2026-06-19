/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "assets/assetManager.h"
#include "scene/object.h"
#include "scene/components/animModel.h"
#include "assets/assetManager.h"
#include <t3d/t3dmodel.h>
#include <libdragon.h> // asset_fopen / fclose for lazy animation file handles

#include "../../renderer/bigtex/bigtex.h"
#include "renderer/material.h"
#include "scene/scene.h"
#include "scene/sceneManager.h"

#include "lib/animController.h"

namespace
{
  struct InitData
  {
    uint16_t assetIdx;
    uint8_t layer;
    uint8_t flags;
    P64::Renderer::MaterialInstance material;
  };
}

namespace P64::Comp
{
  void AnimModel::activateAnim(int16_t idx, T3DSkeleton* skel) {
    if (anims[idx].file == nullptr) {
      anims[idx].file = asset_fopen(anims[idx].animRef->filePath, nullptr);
    }
    t3d_anim_attach(&anims[idx], skel);
    t3d_anim_set_time(&anims[idx], 0.0f); // restart from frame 0 on (re)selection
  }

  void AnimModel::deactivateAnim(int16_t idx) {
    if (idx < 0) return;
    if (idx == animIdxMain || idx == animIdxBlend) return; // still in use by the other slot
    if (anims[idx].file) {
      fclose(anims[idx].file);
      anims[idx].file = nullptr;
    }
  }

  void AnimModel::setMainAnim(int16_t idx) {
    if (idx == animIdxMain) {
      mainAnimDuration = idx >= 0 ? anims[idx].animRef->duration : 0.0f;
      return;
    }
    int16_t old = animIdxMain;
    animIdxMain = idx; // set before deactivate so its guard sees the new selection
    if (idx >= 0) activateAnim(idx, &skelMain);
    deactivateAnim(old);
    mainAnimDuration = idx >= 0 ? anims[idx].animRef->duration : 0.0f;
  }

  void AnimModel::setBlendAnim(int16_t idx) {
    if (idx < 0) {
      int16_t old = animIdxBlend;
      animIdxBlend = -1;
      blendAnimDuration = 0.0f;
      deactivateAnim(old);
      return;
    }
    if (animIdxBlend != idx) {
      int16_t old = animIdxBlend;
      animIdxBlend = idx; // set before deactivate so its guard sees the new selection

      activateAnim(idx, &skelAnim[idx]); // blend attaches to its per-anim skeleton clone

      // Snap blend anim to the same normalized phase as main so they start in sync
      if (animIdxMain >= 0 && mainAnimDuration > 0.0f) {
        float normPhase = anims[animIdxMain].time / mainAnimDuration;
        float blendDur = anims[idx].animRef->duration;
        t3d_anim_set_time(&anims[idx], normPhase * blendDur);
      }

      deactivateAnim(old);
    }
    blendAnimDuration = anims[idx].animRef->duration;
  }


  void AnimModel::swapModel(uint16_t assetIdx) {
    if (anims) {
      auto it = t3d_model_iter_create(model, T3D_CHUNK_TYPE_ANIM);
      uint32_t i = 0;
      while(t3d_model_iter_next(&it)) {
        t3d_anim_destroy(&anims[i]);
        t3d_skeleton_destroy(&skelAnim[i]);
        ++i;
      }
      t3d_skeleton_destroy(&skelMain);
      free(skelAnim);
      free(anims);
      skelAnim = nullptr;
      anims    = nullptr;
    }

    model = (T3DModel*)AssetManager::getByIndex(assetIdx);
    assert(model != nullptr);

    animIdxMain       = -1;
    animIdxBlend      = -1;
    mainAnimDuration  = 0.0f;
    blendAnimDuration = 0.0f;
    blendFactor       = 0.0f;

    auto animCount = t3d_model_get_animation_count(model);
    skelMain = t3d_skeleton_create_buffered(model, 3);
    skelAnim = static_cast<T3DSkeleton*>(malloc(sizeof(T3DSkeleton) * animCount));
    anims    = static_cast<T3DAnim*>(malloc(sizeof(T3DAnim) * animCount));
    t3d_skeleton_update(&skelMain);

    auto it = t3d_model_iter_create(model, T3D_CHUNK_TYPE_ANIM);
    uint32_t i = 0;
    while(t3d_model_iter_next(&it)) {
      skelAnim[i] = t3d_skeleton_clone(&skelMain, false);
      anims[i]    = t3d_anim_create(model, it.anim->name);
      // Lazy file handles: release until the anim is selected (see activateAnim/deactivateAnim)
      if (anims[i].file) { fclose(anims[i].file); anims[i].file = nullptr; }
      ++i;
    }

    if (!model->userBlock) {
      Renderer::MaterialState state{};
      rspq_block_begin();
      auto boneSeg = (const T3DMat4FP*)t3d_segment_placeholder(T3D_SEGMENT_SKELETON);
      it = t3d_model_iter_create(model, T3D_CHUNK_TYPE_OBJECT);
      while(t3d_model_iter_next(&it)) {
        auto *mat = (P64::Renderer::Material*)it.object->material;
        assert(mat);
        mat->begin(state);
        t3d_model_draw_object(it.object, boneSeg);
        mat->end(state);
      }
      model->userBlock = rspq_block_end();
    }
  }

  uint32_t AnimModel::getAllocSize(uint16_t* initData)
  {
    return sizeof(AnimModel) - sizeof(Renderer::MaterialInstance) + ((InitData*)initData)->material.getSize();
  }

  void AnimModel::initDelete([[maybe_unused]] Object& obj, AnimModel* data, void* initData_)
  {
    auto *initData = (InitData*)initData_;
    if (initData == nullptr) {
      auto it = t3d_model_iter_create(data->model, T3D_CHUNK_TYPE_ANIM);
      uint32_t i=0;
      while(t3d_model_iter_next(&it)) {
        t3d_anim_destroy(&data->anims[i]);
        t3d_skeleton_destroy(&data->skelAnim[i]);
        ++i;
      }
      t3d_skeleton_destroy(&data->skelMain);
      free(data->skelAnim);
      free(data->anims);

      data->~AnimModel();
      return;
    }

    new(data) AnimModel();

    data->model = (T3DModel*)AssetManager::getByIndex(initData->assetIdx);
    assert(data->model != nullptr);
    data->layerIdx = initData->layer;
    data->flags = initData->flags;

    // struct has move/copy removed for safety and to avoid accidental copies.
    // but we still need to memcpy here, the warning is wrong anyways as it's still a trivial type
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wclass-memaccess"
      memcpy(&data->material, &initData->material, initData->material.getSize());
    #pragma GCC diagnostic pop

    data->material.init();

    /*bool isBigTex = SceneManager::getCurrent().getConf().pipeline == SceneConf::Pipeline::BIG_TEX_256;

    if(isBigTex) {
      Renderer::BigTex::patchT3DM(*data->model);
      return;
    }*/

    // @TODO: all of this is rather hacky and unoptimized
    // @TODO: refactor and rethink animations here

    auto animCount = t3d_model_get_animation_count(data->model);

    // one main skeleton for drawing, others for potential blending
    data->skelMain = t3d_skeleton_create_buffered(data->model, 3); // @TODO: take from scene settings once added
    data->skelAnim = static_cast<T3DSkeleton*>(malloc(sizeof(T3DSkeleton) * animCount));
    data->anims = static_cast<T3DAnim*>(malloc(sizeof(T3DAnim) * animCount));

    t3d_skeleton_update(&data->skelMain);

    auto it = t3d_model_iter_create(data->model, T3D_CHUNK_TYPE_ANIM);
    uint32_t i = 0;

    // @TODO: handles names vs indices in the public API

    //debugf("AnimModel: count=%lu\n", animCount);
    while(t3d_model_iter_next(&it)) {
      data->skelAnim[i] = t3d_skeleton_clone(&data->skelMain, false);
      data->anims[i] = t3d_anim_create(data->model, it.anim->name); // @TOOD: add  create by-index to t3d API
      // Lazy file handles: only the selected main/blend anim keeps its .sdata FILE* open
      // (see activateAnim/deactivateAnim). Release the handle t3d_anim_create just opened and
      // defer attach until selection — otherwise every anim of every object eats one of
      // libdragon's 64 libc-lock slots and a busy scene exhausts the pool.
      if (data->anims[i].file) { fclose(data->anims[i].file); data->anims[i].file = nullptr; }
      //debugf(" - %s: %lu\n", it.anim->name, i);
      ++i;
    }

    Renderer::MaterialState state{};

    if(data->model->userBlock)return; // already recorded the model
    rspq_block_begin();

    auto boneSeg = (const T3DMat4FP*)t3d_segment_placeholder(T3D_SEGMENT_SKELETON);
    it = t3d_model_iter_create(data->model, T3D_CHUNK_TYPE_OBJECT);
    while(t3d_model_iter_next(&it))
    {
      auto *mat = (P64::Renderer::Material*)it.object->material;
      assert(mat);
      mat->begin(state);
      t3d_model_draw_object(it.object, boneSeg);
      mat->end(state);
    }

    data->model->userBlock = rspq_block_end();
  }

  void AnimModel::update(Object& obj, AnimModel* data, float deltaTime)
  {
    uint8_t framesSinceUpdate;
    if (!data->shouldUpdate(AnimController::getFrameCount(), framesSinceUpdate)) {
      data->flags |= 1;
      return;
    }
    data->flags &= ~1;

    if (framesSinceUpdate == 0) return;
    if (data->animIdxMain < 0) return;

    const float dt = deltaTime * static_cast<float>(framesSinceUpdate);

    const float alpha = data->blendFactor;

    if (data->animIdxBlend >= 0)
    {
      float durMain  = data->mainAnimDuration  > 0.0f ? data->mainAnimDuration  : 1.0f;
      float durBlend = data->blendAnimDuration > 0.0f ? data->blendAnimDuration : 1.0f;

      // Both animations advance at the same normalized phase rate so their cycles stay locked.
      // Phase rate (cycles/sec) = (1-alpha)/durMain + alpha/durBlend
      // dt_main  = phase_rate * durMain  * dt = ((1-alpha) + alpha*(durMain/durBlend)) * dt
      // dt_blend = phase_rate * durBlend * dt = ((1-alpha)*(durBlend/durMain) + alpha) * dt
      float dtMain  = ((1.0f - alpha) + alpha * (durMain  / durBlend)) * dt;
      float dtBlend = ((1.0f - alpha) * (durBlend / durMain) + alpha)  * dt;

      // Always update both so their phases never drift apart, even when alpha is 0 or 1
      t3d_anim_update(&data->anims[data->animIdxMain],  dtMain);
      t3d_anim_update(&data->anims[data->animIdxBlend], dtBlend);

      if (alpha > 0.001f) {
        t3d_skeleton_blend(
          &data->skelMain,
          &data->skelMain,
          &data->skelAnim[data->animIdxBlend],
          alpha
        );
      }
    }
    else
    {
      t3d_anim_update(&data->anims[data->animIdxMain], dt);
    }

    t3d_skeleton_update(&data->skelMain);
  }

  void AnimModel::draw(Object &obj, AnimModel* data, float deltaTime)
  {
    if(data->flags & (1)) {
      // animation was not updated this frame by request, so lets not even bother drawing it.
      return;
    }
    auto mat = data->matFP.getNext();
    t3d_mat4fp_from_srt(mat, obj.scale, obj.rot, obj.pos);

    if(data->layerIdx)DrawLayer::use3D(data->layerIdx);

    data->material.begin(obj);

    t3d_skeleton_use(&data->skelMain);
    t3d_matrix_set(mat, true);
    rspq_block_run(data->model->userBlock);

    data->material.end();
    if(data->layerIdx)DrawLayer::useDefault();
  }
}
