#include "lib/animController.h"

#include "scene/components/animModel.h"

#include <unordered_map>
#include <algorithm>
#include <vector>

namespace
{
  struct AnimData{
    uint64_t frameLastUpdated{0};
    uint8_t staggerRate{1};
  };
    
  constinit std::unordered_map<const P64::Comp::AnimModel*, AnimData>* animDataMap = nullptr;
  constinit uint64_t globalFrameCounter = 0;
}

namespace P64 {
  namespace AnimController {

    void init(){
      animDataMap = new std::unordered_map<const P64::Comp::AnimModel*, AnimData>();
      globalFrameCounter = 0;
    }
  
    void destroy(){
      delete animDataMap;
    }
  
    void update([[maybe_unused]] float deltaTime){
      ++globalFrameCounter;
    }

    void draw_registered(){
      float posY = 54.0f;
      float stepY = 8.0f;
      rdpq_text_printf(nullptr, 1, 10, posY, "Animation(x%u) frame: %llu", animDataMap->size(), globalFrameCounter);
      posY += stepY;
      for(const auto& pair : *animDataMap)
      {                
          const P64::Comp::AnimModel* anim = pair.first;
          const AnimData& data = pair.second;
          rdpq_text_printf(nullptr, 1, 15, posY, "%p: Rate = %d, Last Update = %llu", anim, data.staggerRate, globalFrameCounter - data.frameLastUpdated);
          posY += stepY;
      }
    }
  
    void setAnimStagger(const P64::Comp::AnimModel &anim, uint8_t staggerRate){
      (*animDataMap)[&anim].staggerRate = staggerRate;
      (*animDataMap)[&anim].frameLastUpdated = globalFrameCounter > staggerRate ? globalFrameCounter - staggerRate : 0;
    }
    
    void clearAnimStagger(const P64::Comp::AnimModel &anim){
      animDataMap->erase(&anim);
    }
  
    bool canAnimUpdate(const P64::Comp::AnimModel &anim, uint8_t &framesSinceUpdate){
      auto it = animDataMap->find(&anim);
      if(it == animDataMap->end()) {
        // not found means the animation is not staggered, so it can update every frame
        framesSinceUpdate = 1;
        return true; 
      }
  
      AnimData &data = it->second;
      if(data.staggerRate == 0) {
        framesSinceUpdate = 0;
        return false; // explicitly disabled
      }

      uint8_t framesSinceLastUpdate = globalFrameCounter - data.frameLastUpdated;  
      if(framesSinceLastUpdate >= data.staggerRate) {
            framesSinceUpdate = framesSinceLastUpdate;
            data.frameLastUpdated = globalFrameCounter;
      } else framesSinceUpdate = 0;
      return true;
    }

  } // AnimController
} // P64