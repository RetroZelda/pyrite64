/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include "events/types.h"
#include <t3d/t3dmodel.h>

#include "assets/assetManager.h"
#include "audio/audioManager.h"
#include "lib/matrixManager.h"
#include "scene/object.h"
#include "scene/sceneManager.h"
#include "script/scriptTable.h"

namespace P64::Comp
{
  struct Audio2D
  {
    static constexpr uint32_t ID = 6;

    static constexpr uint8_t FLAG_LOOP = 1 << 0;
    static constexpr uint8_t FLAG_AUTO_PLAY = 1 << 1;
    static constexpr uint8_t FLAG_TYPE_XM = 1 << 2;

    union
    {
      wav64_t *audioWAV{};
      xm64player_t *audioXM;
    };

    float volume{1.0f};
    uint8_t flags{0};
    bool playing{false}; // Fork: event graph, for the Finished output
    Audio::Handle handle{};

    static uint32_t getAllocSize([[maybe_unused]] uint16_t* initData)
    {
      return sizeof(Audio2D);
    }

    static void initDelete([[maybe_unused]] Object& obj, Audio2D* data, uint16_t* initData);

    static void update(Object& obj, Audio2D* data, float deltaTime);
    static void onInput(Object& obj, Audio2D* data, uint8_t pin, const Events::Event& ev); // Fork: event graph

    constexpr bool isXM() const {
      return (flags & FLAG_TYPE_XM) != 0;
    }
  };
}