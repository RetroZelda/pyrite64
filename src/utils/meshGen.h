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
  void generateGrid(Renderer::Mesh &mesh, int size, float thickness = 0.25f);

  void addLineSphere(
    Renderer::Mesh &mesh,
    const glm::vec3 &pos, const glm::vec3 &halfExtend,
    const glm::vec4 &color = {1.0f, 1.0f, 1.0f, 1.0f},
    const glm::quat &rot = glm::quat{1,0,0,0},
    float thickness = 0.25f
  );
  void addLineBox(
    Renderer::Mesh &mesh,
    const glm::vec3 &pos, const glm::vec3 &halfExtend,
    const glm::vec4 &color = {1.0f, 1.0f, 1.0f, 1.0f},
    const glm::quat &rot = glm::quat{1,0,0,0},
    float thickness = 0.25f
  );

  void addLineCylinder(
    Renderer::Mesh &mesh,
    const glm::vec3 &pos, const glm::vec3 &halfExtend,
    const glm::vec4 &color = {1.0f, 1.0f, 1.0f, 1.0f},
    const glm::quat &rot = glm::quat{1,0,0,0},
    float thickness = 0.25f
  );

  void addLineCapsule(
    Renderer::Mesh &mesh,
    const glm::vec3 &pos, const glm::vec3 &halfExtend,
    const glm::vec4 &color = {1.0f, 1.0f, 1.0f, 1.0f},
    const glm::quat &rot = glm::quat{1,0,0,0},
    float thickness = 0.25f
  );

  void addLineCone(
    Renderer::Mesh &mesh,
    const glm::vec3 &pos, const glm::vec3 &halfExtend,
    const glm::vec4 &color = {1.0f, 1.0f, 1.0f, 1.0f},
    const glm::quat &rot = glm::quat{1,0,0,0},
    float thickness = 0.25f
  );

  void addLinePyramid(
    Renderer::Mesh &mesh,
    const glm::vec3 &pos, const glm::vec3 &halfExtend,
    const glm::vec4 &color = {1.0f, 1.0f, 1.0f, 1.0f},
    const glm::quat &rot = glm::quat{1,0,0,0},
    float thickness = 0.25f
  );

  void addLine(
    Renderer::Mesh &mesh,
    const glm::vec3 &start,
    const glm::vec3 &end,
    const glm::vec4 &color = {1.0f, 1.0f, 1.0f, 1.0f}, 
    float thickness = 0.2f
  );

  void addSprite(
    Renderer::Mesh &mesh,
    const glm::vec3 &pos,
    uint32_t objectId,
    uint8_t spriteIdx,
    const glm::vec4 &color = {1.0f, 1.0f, 1.0f, 1.0f}
  );
}