/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "scene/object.h"
#include "scene/components/audio2d.h"

#include "audio/audioManager.h"
#include "scene/sceneManager.h"
#include "events/bus.h"
#include "events/pins.h"

namespace
{
  struct InitData
  {
    uint16_t assetIdx;
    uint16_t volume;
    uint8_t flags;
    uint8_t padding;
  };
}

namespace P64::Comp
{
  void Audio2D::initDelete(Object &obj, Audio2D* data, uint16_t* initData_)
  {
    auto initData = (InitData*)initData_;
    if (initData == nullptr) {
      data->~Audio2D();
      return;
    }

    new(data) Audio2D();

    data->volume = (float)initData->volume * (1.0f / 0xFFFF);
    data->flags = initData->flags;
    bool autoPlay = data->flags & FLAG_AUTO_PLAY;

    if(data->isXM())
    {
      data->audioXM = (xm64player_t*)AssetManager::getByIndex(initData->assetIdx);
      assert(data->audioXM);
      xm64player_set_loop(data->audioXM, (data->flags & FLAG_LOOP) != 0);

      if(autoPlay)data->handle = AudioManager::play2D(data->audioXM);
    } else {
      data->audioWAV = (wav64_t*)AssetManager::getByIndex(initData->assetIdx);
      assert(data->audioWAV);
      wav64_set_loop(data->audioWAV, (data->flags & FLAG_LOOP) != 0);

      if(autoPlay)data->handle = AudioManager::play2D(data->audioWAV);
    }

    if(autoPlay)data->handle.setVolume(data->volume);
    data->playing = autoPlay;
  }

  void Audio2D::update(Object& obj, Audio2D* data, [[maybe_unused]] float deltaTime)
  {
    // Fork: event graph, the Finished output
    if(data->playing && data->handle.isDone()) {
      data->playing = false;
      Events::fire(obj, data, static_cast<uint8_t>(Events::Pins::AudioOut::Finished));
    }
  }

  void Audio2D::onInput([[maybe_unused]] Object& obj, Audio2D* data, uint8_t pin, [[maybe_unused]] const Events::Event& ev)
  {
    using In = Events::Pins::AudioIn;
    data->handle.stop();
    data->playing = false;
    if(pin != static_cast<uint8_t>(In::Play))return;

    data->handle = data->isXM() ? AudioManager::play2D(data->audioXM) : AudioManager::play2D(data->audioWAV);
    data->handle.setVolume(data->volume);
    data->playing = true;
  }
}
