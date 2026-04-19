#include "lib/animController.h"

#include "scene/components/animModel.h"

#include <unordered_map>

namespace
{
  struct AnimData{
    uint8_t staggerRate{1};
    uint8_t frameLastUpdated{0};
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
  
    void setAnimStagger(const P64::Comp::AnimModel &anim, uint8_t staggerRate){
      if(staggerRate == 0) animDataMap->erase(&anim);
      else (*animDataMap)[&anim].staggerRate = staggerRate;
    }
  
    uint8_t canAnimUpdate(const P64::Comp::AnimModel &anim){
      auto it = animDataMap->find(&anim);
      if(it == animDataMap->end()) return 1; // not found, means no staggering for this anim
  
      AnimData &data = it->second;
      uint8_t framesSinceLastUpdate = globalFrameCounter - data.frameLastUpdated;
  
      if(framesSinceLastUpdate >= data.staggerRate) {
          data.frameLastUpdated = globalFrameCounter;
          return framesSinceLastUpdate;
      }
      return 0;
    }

  } // AnimController
} // P64