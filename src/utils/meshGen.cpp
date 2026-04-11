/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "meshGen.h"

#include <cmath>
#include <glm/gtc/quaternion.hpp>

#include "../renderer/mesh.h"

void Utils::Mesh::generateCube(Renderer::Mesh&mesh, float size) {
  mesh.vertices.clear();
  mesh.indices.clear();

  for (int i=0; i<6; i++) {
    glm::vec3 normal{};
    glm::vec3 tangent{};
    glm::vec3 bitangent{};

    switch(i) {
      case 0: normal = {0, 0, 1}; tangent = {1, 0, 0}; bitangent = {0, 1, 0}; break; // front
      case 1: normal = {0, 0, -1}; tangent = {-1, 0, 0}; bitangent = {0, 1, 0}; break; // back
      case 2: normal = {1, 0, 0}; tangent = {0, 0, -1}; bitangent = {0, 1, 0}; break; // right
      case 3: normal = {-1, 0, 0}; tangent = {0, 0, 1}; bitangent = {0, 1, 0}; break; // left
      case 4: normal = {0, 1, 0}; tangent = {1, 0, 0}; bitangent = {0, 0, -1}; break; // top
      case 5: normal = {0, -1, 0}; tangent = {1, 0, 0}; bitangent = {0, 0, 1}; break; // bottom
    }

    uint16_t startIdx = mesh.vertices.size();

    auto v0 = glm::ivec3((normal - tangent - bitangent) * size * 0.5f);
    auto v1 = glm::ivec3((normal + tangent - bitangent) * size * 0.5f);
    auto v2 = glm::ivec3((normal + tangent + bitangent) * size * 0.5f);
    auto v3 = glm::ivec3((normal - tangent + bitangent) * size * 0.5f);

    uint16_t norm = 0;
    mesh.vertices.push_back({v0, norm, {0xFF, 0xFF,0xFF,0xFF}, {0,0}});
    mesh.vertices.push_back({v1, norm, {0xFF, 0xFF,0xFF,0xFF}, {32,0}});
    mesh.vertices.push_back({v2, norm, {0xFF, 0xFF,0xFF,0xFF}, {32,32}});
    mesh.vertices.push_back({v3, norm, {0xFF, 0xFF,0xFF,0xFF}, {0,32}});

    mesh.indices.push_back(startIdx + 2);
    mesh.indices.push_back(startIdx + 0);
    mesh.indices.push_back(startIdx + 1);

    mesh.indices.push_back(startIdx + 0);
    mesh.indices.push_back(startIdx + 2);
    mesh.indices.push_back(startIdx + 3);
  }
}

void Utils::Mesh::generateGrid(Renderer::Mesh&mesh, int size) {

  glm::u8vec4 col{0x80,0x80,0x80,0xFF};
  glm::u8vec4 colX{0xFF, 0, 0, 0xFF};
  glm::u8vec4 colY{0, 0xFF, 0, 0xFF};
  glm::u8vec4 colZ{0, 0, 0xFF, 0xFF};


  for (int z=-size; z<=size; z++) {
    mesh.vertLines.push_back({{ -size, 0.0f, (float)z }, 0, z == 0 ? colX : col});
    mesh.vertLines.push_back({{  size, 0.0f, (float)z }, 0, z == 0 ? colX : col});
    mesh.indices.push_back(mesh.vertLines.size() - 2);
    mesh.indices.push_back(mesh.vertLines.size() - 1);
  }
  for (int x=-size; x<=size; x++) {
    mesh.vertLines.push_back({{ (float)x, 0.0f, -size }, 0, x == 0 ? colZ : col});
    mesh.vertLines.push_back({{ (float)x, 0.0f,  size }, 0, x == 0 ? colZ : col});
    mesh.indices.push_back(mesh.vertLines.size() - 2);
    mesh.indices.push_back(mesh.vertLines.size() - 1);
  }

  mesh.vertLines.push_back({{ 0.0f, -size, 0.0f }, 0, colY});
  mesh.vertLines.push_back({{ 0.0f,  size, 0.0f }, 0, colY});
  mesh.indices.push_back(mesh.vertLines.size() - 2);
  mesh.indices.push_back(mesh.vertLines.size() - 1);
}

void Utils::Mesh::addLineSphere(Renderer::Mesh &mesh, const glm::vec3 &pos, const glm::vec3 &halfExtend,
  const glm::u8vec4 &color, const glm::quat &rot)
{
  auto toWorld = [&pos, &rot](const glm::vec3 &p) {
    return pos + (rot * p);
  };

  float radius = fmaxf(halfExtend.x, fmaxf(halfExtend.y, halfExtend.z));
  int steps = 16;
  float step = 2.0f * (float)M_PI / steps;
  glm::vec3 last = toWorld({radius, 0, 0});
  for(int i=1; i<=steps; ++i) {
    float angle = i * step;
    glm::vec3 next = toWorld({radius * cosf(angle), 0, radius * sinf(angle)});
    addLine(mesh, last, next, color);
    last = next;
  }
  last = toWorld({0, radius, 0});
  for(int i=1; i<=steps; ++i) {
    float angle = i * step;
    glm::vec3 next = toWorld({0, radius * cosf(angle), radius * sinf(angle)});
    addLine(mesh, last, next, color);
    last = next;
  }
  last = toWorld({radius, 0, 0});
  for(int i=1; i<=steps; ++i) {
    float angle = i * step;
    glm::vec3 next = toWorld({radius * cosf(angle), radius * sinf(angle), 0});
    addLine(mesh, last, next, color);
    last = next;
  }
}

void Utils::Mesh::addLineBox(
  Renderer::Mesh&mesh, const glm::vec3&pos, const glm::vec3&halfExtend,
  const glm::u8vec4 &color, const glm::quat &rot
) {
  auto toWorld = [&pos, &rot](const glm::vec3 &p) {
    return pos + (rot * p);
  };

  uint16_t startIdx = mesh.vertLines.size();

  glm::vec3 v0 = toWorld({-halfExtend.x, -halfExtend.y, -halfExtend.z});
  glm::vec3 v1 = toWorld({ halfExtend.x, -halfExtend.y, -halfExtend.z});
  glm::vec3 v2 = toWorld({ halfExtend.x,  halfExtend.y, -halfExtend.z});
  glm::vec3 v3 = toWorld({-halfExtend.x,  halfExtend.y, -halfExtend.z});
  glm::vec3 v4 = toWorld({-halfExtend.x, -halfExtend.y,  halfExtend.z});
  glm::vec3 v5 = toWorld({ halfExtend.x, -halfExtend.y,  halfExtend.z});
  glm::vec3 v6 = toWorld({ halfExtend.x,  halfExtend.y,  halfExtend.z});
  glm::vec3 v7 = toWorld({-halfExtend.x,  halfExtend.y,  halfExtend.z});

  mesh.vertLines.push_back({v0, 0, color});
  mesh.vertLines.push_back({v1, 0, color});
  mesh.vertLines.push_back({v2, 0, color});
  mesh.vertLines.push_back({v3, 0, color});
  mesh.vertLines.push_back({v4, 0, color});
  mesh.vertLines.push_back({v5, 0, color});
  mesh.vertLines.push_back({v6, 0, color});
  mesh.vertLines.push_back({v7, 0, color});

  constexpr uint16_t INDICES[] = {
    0,1, 1,2, 2,3, 3,0,
    4,5, 5,6, 6,7, 7,4,
    0,4, 1,5, 2,6, 3,7
  };
  for (auto idx : INDICES) {
    mesh.indices.push_back(startIdx + idx);
  }
}

void Utils::Mesh::addLineCylinder(Renderer::Mesh &mesh, const glm::vec3 &pos, const glm::vec3 &halfExtend,
  const glm::u8vec4 &color, const glm::quat &rot)
{
  auto toWorld = [&pos, &rot](const glm::vec3 &p) {
    return pos + (rot * p);
  };

  float radius = fmaxf(halfExtend.x, halfExtend.z);
  int steps = 16;
  float step = 2.0f * (float)M_PI / steps;

  glm::vec3 lastTop = toWorld({radius, halfExtend.y, 0});
  glm::vec3 lastBottom = toWorld({radius, -halfExtend.y, 0});
  for(int i=1; i<=steps; ++i) {
    float angle = i * step;
    glm::vec3 nextTop = toWorld({radius * cosf(angle), halfExtend.y, radius * sinf(angle)});
    glm::vec3 nextBottom = toWorld({radius * cosf(angle), -halfExtend.y, radius * sinf(angle)});
    addLine(mesh, lastTop, nextTop, color);
    addLine(mesh, lastBottom, nextBottom, color);
    addLine(mesh, lastTop, lastBottom, color);
    lastTop = nextTop;
    lastBottom = nextBottom;
  }
}

void Utils::Mesh::addLineCapsule(Renderer::Mesh &mesh, const glm::vec3 &pos, const glm::vec3 &halfExtend, const glm::u8vec4 &color, const glm::quat &rot)
{
  auto toWorld = [&pos, &rot](const glm::vec3 &p) {
    return pos + (rot * p);
  };

  float radius = fmaxf(halfExtend.x, halfExtend.z);
  int steps = 16;
  int hemiRings = 4;
  float step = 2.0f * (float)M_PI / steps;

  glm::vec3 topCenter{0, halfExtend.y, 0};
  glm::vec3 bottomCenter{0, -halfExtend.y, 0};

  // Equator circles where the cylinder meets each hemisphere.
  glm::vec3 lastTopEq = toWorld(topCenter + glm::vec3{radius, 0, 0});
  glm::vec3 lastBottomEq = toWorld(bottomCenter + glm::vec3{radius, 0, 0});
  for(int i = 1; i <= steps; ++i) {
    float angle = i * step;
    glm::vec3 nextTopEq = toWorld(topCenter + glm::vec3{radius * cosf(angle), 0, radius * sinf(angle)});
    glm::vec3 nextBottomEq = toWorld(bottomCenter + glm::vec3{radius * cosf(angle), 0, radius * sinf(angle)});
    addLine(mesh, lastTopEq, nextTopEq, color);
    addLine(mesh, lastBottomEq, nextBottomEq, color);
    lastTopEq = nextTopEq;
    lastBottomEq = nextBottomEq;
  }

  // Longitudinal body lines to communicate the cylindrical section.
  for(int i = 0; i < 4; ++i) {
    float angle = (float)i * ((float)M_PI * 0.5f);
    glm::vec3 ringOffset{radius * cosf(angle), 0, radius * sinf(angle)};
    addLine(mesh, toWorld(topCenter + ringOffset), toWorld(bottomCenter + ringOffset), color);
  }

  // Hemisphere rings + meridian connectors for the top and bottom caps.
  for(int r = 1; r <= hemiRings; ++r) {
    float phi = ((float)r / (float)hemiRings) * ((float)M_PI * 0.5f);
    float ringRadius = radius * cosf(phi);
    float yOffset = radius * sinf(phi);

    glm::vec3 lastTop = toWorld(topCenter + glm::vec3{ringRadius, yOffset, 0});
    glm::vec3 lastBottom = toWorld(bottomCenter + glm::vec3{ringRadius, -yOffset, 0});
    for(int i = 1; i <= steps; ++i) {
      float angle = i * step;
      glm::vec3 nextTop = toWorld(topCenter + glm::vec3{ringRadius * cosf(angle), yOffset, ringRadius * sinf(angle)});
      glm::vec3 nextBottom = toWorld(bottomCenter + glm::vec3{ringRadius * cosf(angle), -yOffset, ringRadius * sinf(angle)});
      addLine(mesh, lastTop, nextTop, color);
      addLine(mesh, lastBottom, nextBottom, color);
      lastTop = nextTop;
      lastBottom = nextBottom;
    }

    // Connect ring samples so the hemisphere curvature is visible in wireframe.
    for(int i = 0; i < 4; ++i) {
      float angle = (float)i * ((float)M_PI * 0.5f);
      glm::vec3 topOnRing = toWorld(topCenter + glm::vec3{ringRadius * cosf(angle), yOffset, ringRadius * sinf(angle)});
      glm::vec3 bottomOnRing = toWorld(bottomCenter + glm::vec3{ringRadius * cosf(angle), -yOffset, ringRadius * sinf(angle)});

      if(r == 1) {
        glm::vec3 topEq = toWorld(topCenter + glm::vec3{radius * cosf(angle), 0, radius * sinf(angle)});
        glm::vec3 bottomEq = toWorld(bottomCenter + glm::vec3{radius * cosf(angle), 0, radius * sinf(angle)});
        addLine(mesh, topEq, topOnRing, color);
        addLine(mesh, bottomEq, bottomOnRing, color);
      } else {
        float prevPhi = ((float)(r - 1) / (float)hemiRings) * ((float)M_PI * 0.5f);
        float prevRingRadius = radius * cosf(prevPhi);
        float prevYOffset = radius * sinf(prevPhi);
        glm::vec3 prevTop = toWorld(topCenter + glm::vec3{prevRingRadius * cosf(angle), prevYOffset, prevRingRadius * sinf(angle)});
        glm::vec3 prevBottom = toWorld(bottomCenter + glm::vec3{prevRingRadius * cosf(angle), -prevYOffset, prevRingRadius * sinf(angle)});
        addLine(mesh, prevTop, topOnRing, color);
        addLine(mesh, prevBottom, bottomOnRing, color);
      }
    }
  }
}

void Utils::Mesh::addLineCone(Renderer::Mesh &mesh, const glm::vec3 &pos, const glm::vec3 &halfExtend, const glm::u8vec4 &color, const glm::quat &rot)
{
  auto toWorld = [&pos, &rot](const glm::vec3 &p) {
    return pos + (rot * p);
  };

  float radius = fmaxf(halfExtend.x, halfExtend.z);
  int steps = 16;
  float step = 2.0f * (float)M_PI / steps;
  glm::vec3 tip = toWorld({0, halfExtend.y, 0});
  glm::vec3 lastBase = toWorld({radius, -halfExtend.y, 0});
  for (int i = 1; i <= steps; ++i)
  {
    float angle = i * step;
    glm::vec3 nextBase = toWorld({radius * cosf(angle), -halfExtend.y, radius * sinf(angle)});
    addLine(mesh, lastBase, nextBase, color);
    addLine(mesh, tip, lastBase, color);
    lastBase = nextBase;
  }
}

void Utils::Mesh::addLinePyramid(Renderer::Mesh &mesh, const glm::vec3 &pos, const glm::vec3 &halfExtend, const glm::u8vec4 &color, const glm::quat &rot)
{
  auto toWorld = [&pos, &rot](const glm::vec3 &p) {
    return pos + (rot * p);
  };

  float baseSizeX = halfExtend.x;
  float baseSizeZ = halfExtend.z;

  glm::vec3 tip = toWorld({0, halfExtend.y, 0});
  glm::vec3 v0 = toWorld({-baseSizeX, -halfExtend.y, -baseSizeZ});
  glm::vec3 v1 = toWorld({baseSizeX, -halfExtend.y, -baseSizeZ});
  glm::vec3 v2 = toWorld({baseSizeX, -halfExtend.y, baseSizeZ});
  glm::vec3 v3 = toWorld({-baseSizeX, -halfExtend.y, baseSizeZ});
  mesh.vertLines.push_back({v0, 0, color});
  mesh.vertLines.push_back({v1, 0, color});
  mesh.vertLines.push_back({v2, 0, color});
  mesh.vertLines.push_back({v3, 0, color});
  uint16_t startIdx = mesh.vertLines.size() - 4;
  mesh.indices.push_back(startIdx + 0);
  mesh.indices.push_back(startIdx + 1);
  mesh.indices.push_back(startIdx + 1);
  mesh.indices.push_back(startIdx + 2);
  mesh.indices.push_back(startIdx + 2);
  mesh.indices.push_back(startIdx + 3);
  mesh.indices.push_back(startIdx + 3);
  mesh.indices.push_back(startIdx + 0);
  for (int i = 0; i < 4; ++i)
  {
    mesh.indices.push_back(startIdx + i);
    mesh.indices.push_back(mesh.vertLines.size());
  }
  mesh.vertLines.push_back({tip, 0, color});
}

void Utils::Mesh::addLine(Renderer::Mesh &mesh, const glm::vec3 &start, const glm::vec3 &end, const glm::u8vec4 &color)
{
  uint16_t startIdx = mesh.vertLines.size();

  mesh.vertLines.push_back({start, 0, color});
  mesh.vertLines.push_back({end, 0, color});

  mesh.indices.push_back(startIdx + 0);
  mesh.indices.push_back(startIdx + 1);
}

void Utils::Mesh::addSprite(Renderer::Mesh &mesh, const glm::vec3 &pos, uint32_t objectId, uint8_t spriteIdx, const glm::u8vec4 &color)
{
  glm::u8vec4 col{color.r, color.g, color.b, spriteIdx};
  uint16_t idx = mesh.vertLines.size();

  mesh.vertLines.push_back({.pos = pos, .objectId = objectId, .color = col});
  mesh.vertLines.push_back({.pos = pos, .objectId = objectId, .color = col});
  mesh.vertLines.push_back({.pos = pos, .objectId = objectId, .color = col});
  mesh.vertLines.push_back({.pos = pos, .objectId = objectId, .color = col});

  mesh.indices.push_back(idx+0);
  mesh.indices.push_back(idx+1);
  mesh.indices.push_back(idx+2);
  mesh.indices.push_back(idx+2);
  mesh.indices.push_back(idx+3);
  mesh.indices.push_back(idx+0);
}

