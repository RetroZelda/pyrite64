/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once

#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/gtc/quaternion.hpp"


namespace Renderer { class Mesh; }

namespace Utils::Mesh
{
  void generateCube(Renderer::Mesh &mesh, float size);
  void generateGrid(Renderer::Mesh &mesh, int size);

  void addLineSphere(
    Renderer::Mesh &mesh,
    const glm::vec3 &pos, const glm::vec3 &halfExtend,
    const glm::u8vec4 &color = {0xFF,0xFF,0xFF,0xFF},
    const glm::quat &rot = glm::quat{1,0,0,0}
  );
  void addLineBox(
    Renderer::Mesh &mesh,
    const glm::vec3 &pos, const glm::vec3 &halfExtend,
    const glm::u8vec4 &color = {0xFF,0xFF,0xFF,0xFF},
    const glm::quat &rot = glm::quat{1,0,0,0}
  );

  void addLineCylinder(
    Renderer::Mesh &mesh,
    const glm::vec3 &pos, const glm::vec3 &halfExtend,
    const glm::u8vec4 &color = {0xFF,0xFF,0xFF,0xFF},
    const glm::quat &rot = glm::quat{1,0,0,0}
  );

  void addLineCapsule(
    Renderer::Mesh &mesh,
    const glm::vec3 &pos, const glm::vec3 &halfExtend,
    const glm::u8vec4 &color = {0xFF,0xFF,0xFF,0xFF},
    const glm::quat &rot = glm::quat{1,0,0,0}
  );

  void addLineCone(
    Renderer::Mesh &mesh,
    const glm::vec3 &pos, const glm::vec3 &halfExtend,
    const glm::u8vec4 &color = {0xFF,0xFF,0xFF,0xFF},
    const glm::quat &rot = glm::quat{1,0,0,0}
  );

  void addLinePyramid(
    Renderer::Mesh &mesh,
    const glm::vec3 &pos, const glm::vec3 &halfExtend,
    const glm::u8vec4 &color = {0xFF,0xFF,0xFF,0xFF},
    const glm::quat &rot = glm::quat{1,0,0,0}
  );

  void addLine(
    Renderer::Mesh &mesh,
    const glm::vec3 &start,
    const glm::vec3 &end,
    const glm::u8vec4 &color = {0xFF,0xFF,0xFF,0xFF}
  );

  void addSprite(
    Renderer::Mesh &mesh,
    const glm::vec3 &pos,
    uint32_t objectId,
    uint8_t spriteIdx,
    const glm::u8vec4 &color = {0xFF,0xFF,0xFF,0xFF}
  );
}
