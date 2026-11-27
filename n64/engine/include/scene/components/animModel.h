/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include "events/types.h"
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
      uint16_t assetIdx{0};

      T3DSkeleton skelMain{};
      T3DSkeleton *skelAnim{};
      T3DAnim *anims{};
      
      float mainAnimDuration  = 0.0f;
      float blendAnimDuration = 0.0f;

      int16_t animIdxMain{-1};
      int16_t animIdxBlend{-1};

      RingMat4FP matFP{};
      float vertexScale{1.0f};
      uint32_t layerRef{0};
      uint8_t flags{0};
      uint8_t mainWasPlaying{0}; // Fork: event graph, for the MainFinished output

      // Attaches the animation to the skeleton and resets it to frame 0, done on selection
      // (keyframes stream from ROM, so unselected animations hold no file handles).
      void activateAnim(int16_t idx, T3DSkeleton* skel);

      // Fork: the skeletons and the streamed animations of the current model (and their teardown)
      void createAnims();
      void destroyAnims();

    public:
      // 'flags' bits. Fork: FLAG_NO_BLEND comes from the editor ('Blend Skeletons' off): only the main
      // skeleton is allocated, not one clone per clip, so the model cannot blend and setBlendAnim(>= 0)
      // is ignored (with one warning). The other bits are run-time state.
      static constexpr uint8_t FLAG_NOT_UPDATED  = 1 << 0; // skipped this frame by the stagger, not drawn
      static constexpr uint8_t FLAG_NO_BLEND     = 1 << 1;
      static constexpr uint8_t FLAG_BLEND_WARNED = 1 << 2;

      // Stagger fields written by AnimController::setAnimStagger
      uint8_t staggerRate{1};   // 0=disabled, 1=every frame, N=every N frames
      uint8_t staggerOffset{0};
      uint8_t staggerLast{0};   // low 8 bits of globalFrameCounter at last update

      float blendFactor{0.5f};
      Renderer::MaterialInstance material{};

      void setMainAnim(int16_t idx);
      void setBlendAnim(int16_t idx);
      void swapModel(uint16_t newAssetIdx);
      // meters per vertex unit of the current model (bone and vertex data are in vertex units)
      float getVertexScale() const { return vertexScale; }

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
      // nullptr when the model has no blend skeletons ('Blend Skeletons' off)
      const T3DSkeleton* getSkelAnim() const {
        return skelAnim;
      }
      bool canBlend() const { return skelAnim != nullptr; }
      const T3DAnim* getAnims() const {
        return anims;
      }


    // Fork: animation events (docs/todo/anim-events/PLAN.md, section 3.1)

    /** The animation events one model lists: the hash of each pin name, in pin order. */
    struct EventList
    {
      uint16_t assetIdx;
      uint8_t count;
      const uint32_t* pins;
    };

    /** Called once at boot by the generated src/p64/animEvents.cpp. */
    static void setEventLists(const EventList* lists, uint32_t count);

    /**
     * Fires the animation-event output pin 'pin' of this Model (Animated) component, the one named
     * like that in the list of the model it currently shows. The engine never decides when: game
     * code calls this (Kentucky Castle's animation drivers, from the trait events).
     * @param pin the hash of the full pin name: P64::Events::pinHash("Leap.LAND", 9)
     * @param value passed on to the listeners
     * @return false if nothing can hear it: the object has no bindings, or the model does not list
     *         the name (a model without a list is normal, so this is not a warning)
     */
    static bool fireEvent(Object& obj, AnimModel* data, Events::PinName pin, uint32_t value = 0, uint16_t instigator = 0);

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData);

    static void initDelete([[maybe_unused]] Object& obj, AnimModel* data, void* initData);

    static void update(Object& obj, AnimModel* data, [[maybe_unused]] float deltaTime);
    static void onInput(Object& obj, AnimModel* data, uint8_t pin, const Events::Event& ev); // Fork: event graph

    static void draw([[maybe_unused]] Object& obj, AnimModel* data, [[maybe_unused]] float deltaTime);
  };
}