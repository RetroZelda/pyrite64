
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

    /**
        * The animation will be updated in a staggered manner, meaning that not all animations will be updated every frame. 
        * This can be used to improve performance when there are many animations in the scene that do not need to be updated every frame. 
        * The exact staggering pattern is determined by the implementation and may change over time.
        * @param anim The animation to add to the staggered update list. Must not be null.
        * @param staggerRate The rate at which the animation should be updated. A value of 1 means the animation will be updated every frame, 
    *            2 means every other frame, and so on. Default is 1.  Set to 0 to disable staggering for this animation.
    */
    void setAnimStagger(const P64::Comp::AnimModel &anim, uint8_t staggerRate = 1);

    /**
     * Determines if the specified animation can be updated based on the current staggering pattern. 
     * This function should be called each frame for each animation to check if it should be updated.
     * @param anim The animation to check. Must not be null.
     * @return the number of frames since the last update for this animation. 
     *         If the returned value is greater than or equal to the stagger rate set for this animation, it can be updated this frame.
     *         If the returned value is 0, it means the animation is not due for an update this frame.
     */
    uint8_t canAnimUpdate(const P64::Comp::AnimModel &anim);
  }
}