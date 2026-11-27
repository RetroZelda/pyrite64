/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "events/bus.h"
#include "events/pins.h"
#include "assets/assetManager.h"
#include "scene/object.h"
#include "scene/components/animModel.h"
#include "scene/components/eventLinks.h"
#include "assets/assetManager.h"
#include <t3d/t3dmodel.h>

#include "../../renderer/bigtex/bigtex.h"
#include "renderer/material.h"
#include "renderer/renderScale.h"
#include "scene/scene.h"
#include "scene/sceneManager.h"

#include "lib/animController.h"
#include "lib/logger.h"

namespace
{
  // Keyframes are streamed from ROM by DMA into a buffer per animation (split in two halves),
  // tiny3d's default is 512 bytes. Every animation of every object owns one for its whole
  // lifetime, so keep it small.
  constexpr uint32_t ANIM_STREAM_BUFFER_SIZE = 256;

  struct InitData
  {
    uint16_t assetIdx;
    uint8_t flags;
    uint8_t padding;
    uint32_t layerRef;
    P64::Renderer::MaterialInstance material;
  };
}

namespace P64::Comp
{
  void AnimModel::activateAnim(int16_t idx, T3DSkeleton* skel) {
    // Fork: attach only when the clip drives another skeleton (or none) yet. Attaching primes every channel
    // with keyframes streamed from ROM (milliseconds for a big rig); a clip already attached here is rewound.
    const uint8_t where = (skel == &skelMain) ? ATTACHED_MAIN : ATTACHED_BLEND;
    if (!animAttached || animAttached[idx] != where) {
      t3d_anim_attach(&anims[idx], skel);
      if (animAttached) animAttached[idx] = where;
    }
    t3d_anim_set_time(&anims[idx], 0.0f); // restart from frame 0 on (re)selection
  }

  void AnimModel::setMainAnim(int16_t idx) {
    if (idx == animIdxMain) {
      // still the main clip, but a blend request may have moved it onto a blend skeleton since
      if (idx >= 0 && animAttached && animAttached[idx] != ATTACHED_MAIN) activateAnim(idx, &skelMain);
      mainAnimDuration = idx >= 0 ? anims[idx].animRef->duration : 0.0f;
      return;
    }
    animIdxMain = idx;
    if (idx >= 0) activateAnim(idx, &skelMain);
    mainAnimDuration = idx >= 0 ? anims[idx].animRef->duration : 0.0f;
  }

  void AnimModel::setBlendAnim(int16_t idx) {
    if (idx < 0) {
      animIdxBlend = -1;
      blendAnimDuration = 0.0f;
      return;
    }
    if (!skelAnim) {
      // Fork: 'Blend Skeletons' is off for this model, so there is no skeleton to blend into
      if (!(flags & FLAG_BLEND_WARNED)) {
        flags |= FLAG_BLEND_WARNED;
        P64::Log::warn("AnimModel: blend asked for (clip %d) but 'Blend Skeletons' is off on this model, ignored", idx);
      }
      return;
    }
    if (animIdxBlend != idx) {
      animIdxBlend = idx;

      activateAnim(idx, &skelAnim[idx]); // blend attaches to its per-anim skeleton clone

      // Snap blend anim to the same normalized phase as main so they start in sync
      if (animIdxMain >= 0 && mainAnimDuration > 0.0f) {
        float normPhase = anims[animIdxMain].time / mainAnimDuration;
        float blendDur = anims[idx].animRef->duration;
        t3d_anim_set_time(&anims[idx], normPhase * blendDur);
      }
    }
    blendAnimDuration = anims[idx].animRef->duration;
  }


  // Fork: one main skeleton for drawing, plus (unless 'Blend Skeletons' is off) one clone per clip for
  // blending, and a streamed animation per clip.
  void AnimModel::createAnims() {
    auto animCount = t3d_model_get_animation_count(model);
    skelMain = t3d_skeleton_create_buffered(model, 3); // @TODO: take from scene settings once added
    skelAnim = (flags & FLAG_NO_BLEND) ? nullptr
                                       : static_cast<T3DSkeleton*>(malloc(sizeof(T3DSkeleton) * animCount));
    anims    = static_cast<T3DAnim*>(malloc(sizeof(T3DAnim) * animCount));
    animAttached = static_cast<uint8_t*>(calloc(animCount > 0 ? animCount : 1, 1)); // ATTACHED_NONE
    t3d_skeleton_update(&skelMain);

    // @TODO: handles names vs indices in the public API
    auto it = t3d_model_iter_create(model, T3D_CHUNK_TYPE_ANIM);
    uint32_t i = 0;
    while(t3d_model_iter_next(&it)) {
      if(skelAnim)skelAnim[i] = t3d_skeleton_clone(&skelMain, false);
      // @TOOD: add  create by-index to t3d API
      // Attached when selected (see activateAnim), so only the playing animations prime and
      // allocate their targets.
      anims[i] = t3d_anim_create_buffered(model, it.anim->name, ANIM_STREAM_BUFFER_SIZE);
      ++i;
    }
  }

  void AnimModel::destroyAnims() {
    if (!anims) return;
    auto it = t3d_model_iter_create(model, T3D_CHUNK_TYPE_ANIM);
    uint32_t i = 0;
    while(t3d_model_iter_next(&it)) {
      t3d_anim_destroy(&anims[i]);
      if(skelAnim)t3d_skeleton_destroy(&skelAnim[i]);
      ++i;
    }
    t3d_skeleton_destroy(&skelMain);
    free(skelAnim);
    free(anims);
    free(animAttached);
    skelAnim = nullptr;
    anims    = nullptr;
    animAttached = nullptr;
  }

  void AnimModel::swapModel(uint16_t newAssetIdx) {
    destroyAnims();

    if(model)AssetManager::release(assetIdx);
    assetIdx = newAssetIdx;
    model = (T3DModel*)AssetManager::acquire(newAssetIdx);
    assert(model != nullptr);
    vertexScale = AssetManager::getVertexScale(newAssetIdx);

    animIdxMain       = -1;
    animIdxBlend      = -1;
    mainAnimDuration  = 0.0f;
    blendAnimDuration = 0.0f;
    blendFactor       = 0.0f;

    createAnims();

    if (!model->userBlock) {
      Renderer::MaterialState state{};
      rspq_block_begin();
      auto boneSeg = (const T3DMat4FP*)t3d_segment_placeholder(T3D_SEGMENT_SKELETON);
      auto it = t3d_model_iter_create(model, T3D_CHUNK_TYPE_OBJECT);
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
      data->destroyAnims();

      AssetManager::release(data->assetIdx);
      data->~AnimModel();
      return;
    }

    new(data) AnimModel();

    data->assetIdx = initData->assetIdx;
    data->model = (T3DModel*)AssetManager::acquire(initData->assetIdx);
    assert(data->model != nullptr);
    data->vertexScale = AssetManager::getVertexScale(initData->assetIdx);
    data->layerRef = initData->layerRef;
    data->flags = initData->flags & FLAG_NO_BLEND; // Fork: the editor's bits; the rest are run-time state

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

    data->createAnims();

    Renderer::MaterialState state{};

    if(data->model->userBlock)return; // already recorded the model
    rspq_block_begin();

    auto boneSeg = (const T3DMat4FP*)t3d_segment_placeholder(T3D_SEGMENT_SKELETON);
    auto it = t3d_model_iter_create(data->model, T3D_CHUNK_TYPE_OBJECT);
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
      data->flags |= FLAG_NOT_UPDATED;
      return;
    }
    data->flags &= ~FLAG_NOT_UPDATED;

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

    // Fork: event graph, MainFinished when a non-looping main animation stops
    const bool playing = data->anims[data->animIdxMain].isPlaying != 0;
    if(data->mainWasPlaying && !playing) {
      Events::fire(obj, data, static_cast<uint8_t>(Events::Pins::AnimOut::MainFinished), static_cast<uint32_t>(data->animIdxMain));
    }
    data->mainWasPlaying = playing ? 1 : 0;
  }

  // Fork: animation events (docs/todo/anim-events/PLAN.md). The build lists, per model, the hash
  // of each event pin name in pin order; a pin's index is ANIM_EVENT_PIN_BASE + its position.
  namespace
  {
    const AnimModel::EventList* eventLists = nullptr;
    uint32_t eventListCount = 0;
  }

  void AnimModel::setEventLists(const EventList* lists, uint32_t count)
  {
    eventLists = lists;
    eventListCount = count;
  }

  bool AnimModel::fireEvent(Object& obj, AnimModel* data, Events::PinName pin, uint32_t value, uint16_t instigator)
  {
    if(!data || !obj.getComponent<EventLinks>()) return false; // nothing is bound on this object
    for(uint32_t m = 0; m < eventListCount; ++m) {
      const auto &list = eventLists[m];
      if(list.assetIdx != data->assetIdx)continue;
      for(uint8_t p = 0; p < list.count; ++p) {
        if(list.pins[p] != pin.hash)continue;
        Events::fire(obj, data, static_cast<uint8_t>(Events::Pins::ANIM_EVENT_PIN_BASE + p), value, instigator);
        return true;
      }
      return false;
    }
    return false;
  }

  // Fork: event graph pins, see events/pins.h. The value is the clip index.
  void AnimModel::onInput([[maybe_unused]] Object &obj, AnimModel* data, uint8_t pin, const Events::Event& ev)
  {
    using In = Events::Pins::AnimIn;
    const auto clip = static_cast<int16_t>(ev.value);
    if(pin == static_cast<uint8_t>(In::PlayMain)) {
      data->setMainAnim(clip);
      if(auto *anim = data->getMainAnim()) {
        t3d_anim_set_time(anim, 0.0f);
        t3d_anim_set_playing(anim, true);
      }
    } else {
      data->setBlendAnim(clip);
    }
  }

  void AnimModel::draw(Object &obj, AnimModel* data, float deltaTime)
  {
    if(data->flags & FLAG_NOT_UPDATED) {
      // animation was not updated this frame by request, so lets not even bother drawing it.
      return;
    }
    auto mat = data->matFP.getNext();
    Renderer::fillModelMatrixFP(mat, obj.scale, obj.rot, obj.pos, data->vertexScale);

    if(data->layerRef)data->layerRef = DrawLayer::use3D(data->layerRef);

    data->material.begin(obj);

    t3d_skeleton_use(&data->skelMain);
    t3d_matrix_set(mat, true);
    rspq_block_run(data->model->userBlock);

    data->material.end();
    if(data->layerRef)DrawLayer::useDefault();
  }
}
