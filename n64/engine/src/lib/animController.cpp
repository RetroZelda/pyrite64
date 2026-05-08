#include "lib/animController.h"

#include "scene/components/animModel.h"

namespace
{
  constinit uint64_t globalFrameCounter   = 0;
  constinit uint32_t staggerAssignCounter = 0;
}

namespace P64 {
  namespace AnimController {

    void init(){
      globalFrameCounter   = 0;
      staggerAssignCounter = 0;
    }

    void destroy(){}

    void update([[maybe_unused]] float deltaTime){
      ++globalFrameCounter;
    }

    void draw_registered(){
      rdpq_text_printf(nullptr, 1, 10, 54.0f, "Animation frame: %llu", globalFrameCounter);
    }

    uint64_t getFrameCount() {
      return globalFrameCounter;
    }

    void setAnimStagger(P64::Comp::AnimModel &anim, uint8_t staggerRate){
      anim.staggerRate = staggerRate;
      if (staggerRate > 1) {
        // Distribute phase so that N objects with the same rate don't all fire on the same frame.
        uint8_t slot = (uint8_t)(staggerAssignCounter++ % staggerRate);
        anim.staggerLast = (uint8_t)(globalFrameCounter > slot ? globalFrameCounter - slot : 0);
      } else {
        anim.staggerLast = 0;
      }
    }

    void clearAnimStagger(P64::Comp::AnimModel &anim){
      anim.staggerRate   = 1;
      anim.staggerOffset = 0;
      anim.staggerLast   = 0;
    }

  } // AnimController
} // P64
