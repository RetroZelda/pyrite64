/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <t3d/t3dmodel.h>
#include <t3d/t3danim.h>

#include "assets/assetManager.h"
#include "lib/matrixManager.h"
#include "renderer/drawLayer.h"
#include "renderer/material.h"
#include "scene/object.h"
#include "script/scriptTable.h"

namespace P64::Comp
{
  struct AnimModel
  {
    static constexpr uint32_t ID = 10;

    private:
      T3DModel *model{};

      T3DSkeleton skelMain{};
      T3DSkeleton *skelAnim{};
      T3DAnim *anims{};
      
      float mainAnimDuration  = 0.0f;
      float blendAnimDuration = 0.0f;

      int16_t animIdxMain{-1};
      int16_t animIdxBlend{-1};

      RingMat4FP matFP{};
      uint8_t layerIdx{0};
      uint8_t flags{0};

      // Lazy file handles: only the selected main/blend anim keeps its .sdata FILE* open.
      // 'activate' opens (if needed) + attaches + resets to frame 0; 'deactivate' closes the
      // file unless it is still referenced by the other (main/blend) slot.
      void activateAnim(int16_t idx, T3DSkeleton* skel);
      void deactivateAnim(int16_t idx);

    public:
      // Stagger fields written by AnimController::setAnimStagger
      uint8_t staggerRate{1};   // 0=disabled, 1=every frame, N=every N frames
      uint8_t staggerOffset{0};
      uint8_t staggerLast{0};   // low 8 bits of globalFrameCounter at last update

      float blendFactor{0.5f};
      Renderer::MaterialInstance material{};

      void setMainAnim(int16_t idx);
      void setBlendAnim(int16_t idx);
      void swapModel(uint16_t assetIdx);

      bool shouldUpdate(uint64_t globalFrame, uint8_t &framesSince) {
        if (staggerRate == 0) { framesSince = 0; return false; }
        if (staggerRate == 1) { framesSince = 1; return true; }
        uint8_t elapsed = (uint8_t)(globalFrame - staggerLast);
        if (elapsed >= staggerRate) {
          framesSince = elapsed;
          staggerLast = (uint8_t)globalFrame;
          return true;
        }
        framesSince = 0;
        return true;
      }

      Renderer::MaterialInstance& getMatInstance() {
        return material;
      }

      T3DAnim* getMainAnim() {
        if (animIdxMain < 0) return nullptr;
        return anims ? &anims[animIdxMain] : nullptr;
      }

      T3DAnim* getBlendAnim() {
        if (animIdxBlend < 0) return nullptr;
        return anims ? &anims[animIdxBlend] : nullptr;
      }

      T3DAnim* getAnim(int16_t idx) const {
        return anims ? &anims[idx] : nullptr;
      }
      
      float getMainAnimDuration()  const { return mainAnimDuration;  }
      float getBlendAnimDuration() const { return blendAnimDuration; }

      const T3DModel* getModel() const {
        return model;
      }
      const T3DSkeleton& getSkelMain() const {
        return skelMain;
      }
      const T3DSkeleton* getSkelAnim() const {
        return skelAnim;
      }
      const T3DAnim* getAnims() const {
        return anims;
      }


    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData);

    static void initDelete([[maybe_unused]] Object& obj, AnimModel* data, void* initData);

    static void update(Object& obj, AnimModel* data, [[maybe_unused]] float deltaTime);

    static void draw([[maybe_unused]] Object& obj, AnimModel* data, [[maybe_unused]] float deltaTime);
  };
}