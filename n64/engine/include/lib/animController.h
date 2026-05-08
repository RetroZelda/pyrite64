
#pragma once
#include <t3d/t3d.h>

namespace P64::Comp
{
  struct AnimModel;
}

namespace P64 {
  namespace AnimController {
    void init();
    void destroy();
    void update([[maybe_unused]] float deltaTime);
    void draw_registered();

    uint64_t getFrameCount();

    /**
     * Updates the animation at a staggered rate. Animations with the same rate
     * are assigned different phase offsets so their updates spread evenly across
     * frames rather than all firing at once.
     * @param staggerRate  1 = every frame (default), N = every N frames, 0 = disabled (no draw).
     */
    void setAnimStagger(P64::Comp::AnimModel &anim, uint8_t staggerRate = 1);

    /**
     * Resets stagger for this animation back to every-frame updates.
     */
    void clearAnimStagger(P64::Comp::AnimModel &anim);

  } // AnimController
} // P64
