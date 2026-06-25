/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <libdragon.h>

namespace P64::Assets::Type
{
  constexpr uint8_t UNKNOWN = 0;
  constexpr uint8_t IMAGE = 1;
  constexpr uint8_t AUDIO = 2;
  constexpr uint8_t FONT = 3;
  constexpr uint8_t MODEL_3D = 4;
  constexpr uint8_t CODE_OBJ = 5;
  constexpr uint8_t CODE_GLOBAL = 6;
  constexpr uint8_t PREFAB = 7;
  constexpr uint8_t NODE_GRAPH = 8;
  constexpr uint8_t MUSIC_XM = 9;
  constexpr uint8_t CANVAS = 10;   // editor/code-gen only — never baked into the ROM asset table
  constexpr uint8_t EMITTER = 11;  // particle-emitter (.emit64); runtime-loadable

  // NOTE: the editor FileType ordinal is packed into the top 4 bits of the ROM asset
  // table entry (type << 28), so these values MUST stay numerically in sync with
  // Project::FileType, and the field caps at 15.
}