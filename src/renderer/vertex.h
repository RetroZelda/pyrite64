/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

namespace Renderer
{
  struct Vertex
  {
    glm::i16vec3 pos{}; // 16.0 fixed point
    uint16_t norm{};    // 5,5,5 compressed normal
    glm::u8vec4 color{};   // RGBA8 color
    glm::i16vec2 uv{};  // 10.6 fixed point (pixel coords)
    glm::i16vec2 boneIdx{}; // bone index (-1 if unused), second entry is padding
  };

  struct LineVertex
  {
    glm::vec3 pos{};
    uint32_t objectId{};
    glm::u8vec4 color{};
  };

  // World-space, CPU-expanded billboard vertex for the "particles" pipeline (24 bytes).
  struct ParticleVertex
  {
    glm::vec3 pos{};     // world-space corner (camera-facing quad built on the CPU)
    glm::u8vec4 color{}; // RGBA8 (UBYTE4_NORM)
    glm::vec2 uv{};
  };
}
