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

void Utils::Mesh::generateGrid(Renderer::Mesh&mesh, int size, float thickness) {

  glm::vec4 col{0.5f, 0.5f, 0.5f, 1.0f};
  glm::vec4 colX{1.0f, 0.0f, 0.0f, 1.0f};
  glm::vec4 colY{0.0f, 1.0f, 0.0f, 1.0f};
  glm::vec4 colZ{0.0f, 0.0f, 1.0f, 1.0f};

  for (int z=-size; z<=size; z++) {
    glm::vec3 p1{-size, 0.0f, (float)z};
    glm::vec3 p2{ size, 0.0f, (float)z};
    addLine(mesh, p1, p2, z == 0 ? colX : col, thickness);
  }
  for (int x=-size; x<=size; x++) {
    glm::vec3 p1{(float)x, 0.0f, -size};
    glm::vec3 p2{(float)x, 0.0f,  size};
    addLine(mesh, p1, p2, x == 0 ? colZ : col, thickness);
  }

  addLine(mesh, {0.0f, -size, 0.0f}, {0.0f, size, 0.0f}, colY, thickness);
}

void Utils::Mesh::addLineSphere(Renderer::Mesh &mesh, const glm::vec3 &pos, const glm::vec3 &halfExtend,
  const glm::vec4 &color, const glm::quat &rot, float thickness)
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
    addLine(mesh, last, next, color, thickness);
    last = next;
  }
  last = toWorld({0, radius, 0});
  for(int i=1; i<=steps; ++i) {
    float angle = i * step;
    glm::vec3 next = toWorld({0, radius * cosf(angle), radius * sinf(angle)});
    addLine(mesh, last, next, color, thickness);
    last = next;
  }
  last = toWorld({radius, 0, 0});
  for(int i=1; i<=steps; ++i) {
    float angle = i * step;
    glm::vec3 next = toWorld({radius * cosf(angle), radius * sinf(angle), 0});
    addLine(mesh, last, next, color, thickness);
    last = next;
  }
}

void Utils::Mesh::addLineBox(
  Renderer::Mesh&mesh, const glm::vec3&pos, const glm::vec3&halfExtend,
  const glm::vec4 &color, const glm::quat &rot, float thickness
) {
  auto toWorld = [&pos, &rot](const glm::vec3 &p) {
    return pos + (rot * p);
  };

  glm::vec3 v0 = toWorld({-halfExtend.x, -halfExtend.y, -halfExtend.z});
  glm::vec3 v1 = toWorld({ halfExtend.x, -halfExtend.y, -halfExtend.z});
  glm::vec3 v2 = toWorld({ halfExtend.x,  halfExtend.y, -halfExtend.z});
  glm::vec3 v3 = toWorld({-halfExtend.x,  halfExtend.y, -halfExtend.z});
  glm::vec3 v4 = toWorld({-halfExtend.x, -halfExtend.y,  halfExtend.z});
  glm::vec3 v5 = toWorld({ halfExtend.x, -halfExtend.y,  halfExtend.z});
  glm::vec3 v6 = toWorld({ halfExtend.x,  halfExtend.y,  halfExtend.z});
  glm::vec3 v7 = toWorld({-halfExtend.x,  halfExtend.y,  halfExtend.z});

  addLine(mesh, v0, v1, color, thickness);
  addLine(mesh, v1, v2, color, thickness);
  addLine(mesh, v2, v3, color, thickness);
  addLine(mesh, v3, v0, color, thickness);

  addLine(mesh, v4, v5, color, thickness);
  addLine(mesh, v5, v6, color, thickness);
  addLine(mesh, v6, v7, color, thickness);
  addLine(mesh, v7, v4, color, thickness);

  addLine(mesh, v0, v4, color, thickness);
  addLine(mesh, v1, v5, color, thickness);
  addLine(mesh, v2, v6, color, thickness);
  addLine(mesh, v3, v7, color, thickness);
}

void Utils::Mesh::addLineCylinder(Renderer::Mesh &mesh, const glm::vec3 &pos, const glm::vec3 &halfExtend,
  const glm::vec4 &color, const glm::quat &rot, float thickness)
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
    addLine(mesh, lastTop, nextTop, color, thickness);
    addLine(mesh, lastBottom, nextBottom, color, thickness);
    addLine(mesh, lastTop, lastBottom, color, thickness);
    lastTop = nextTop;
    lastBottom = nextBottom;
  }
}

void Utils::Mesh::addLineCapsule(Renderer::Mesh &mesh, const glm::vec3 &pos, const glm::vec3 &halfExtend, const glm::vec4 &color, const glm::quat &rot, float thickness)
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

  // Equator circles
  glm::vec3 lastTopEq = toWorld(topCenter + glm::vec3{radius, 0, 0});
  glm::vec3 lastBottomEq = toWorld(bottomCenter + glm::vec3{radius, 0, 0});
  for(int i = 1; i <= steps; ++i) {
    float angle = i * step;
    glm::vec3 nextTopEq = toWorld(topCenter + glm::vec3{radius * cosf(angle), 0, radius * sinf(angle)});
    glm::vec3 nextBottomEq = toWorld(bottomCenter + glm::vec3{radius * cosf(angle), 0, radius * sinf(angle)});
    addLine(mesh, lastTopEq, nextTopEq, color, thickness);
    addLine(mesh, lastBottomEq, nextBottomEq, color, thickness);
    lastTopEq = nextTopEq;
    lastBottomEq = nextBottomEq;
  }

  // Longitudinal body lines
  for(int i = 0; i < 4; ++i) {
    float angle = (float)i * ((float)M_PI * 0.5f);
    glm::vec3 ringOffset{radius * cosf(angle), 0, radius * sinf(angle)};
    addLine(mesh, toWorld(topCenter + ringOffset), toWorld(bottomCenter + ringOffset), color, thickness);
  }

  // Hemisphere rings + meridian connectors
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
      addLine(mesh, lastTop, nextTop, color, thickness);
      addLine(mesh, lastBottom, nextBottom, color, thickness);
      lastTop = nextTop;
      lastBottom = nextBottom;
    }

    // Connect ring samples
    for(int i = 0; i < 4; ++i) {
      float angle = (float)i * ((float)M_PI * 0.5f);
      glm::vec3 topOnRing = toWorld(topCenter + glm::vec3{ringRadius * cosf(angle), yOffset, ringRadius * sinf(angle)});
      glm::vec3 bottomOnRing = toWorld(bottomCenter + glm::vec3{ringRadius * cosf(angle), -yOffset, ringRadius * sinf(angle)});

      if(r == 1) {
        glm::vec3 topEq = toWorld(topCenter + glm::vec3{radius * cosf(angle), 0, radius * sinf(angle)});
        glm::vec3 bottomEq = toWorld(bottomCenter + glm::vec3{radius * cosf(angle), 0, radius * sinf(angle)});
        addLine(mesh, topEq, topOnRing, color, thickness);
        addLine(mesh, bottomEq, bottomOnRing, color, thickness);
      } else {
        float prevPhi = ((float)(r - 1) / (float)hemiRings) * ((float)M_PI * 0.5f);
        float prevRingRadius = radius * cosf(prevPhi);
        float prevYOffset = radius * sinf(prevPhi);
        glm::vec3 prevTop = toWorld(topCenter + glm::vec3{prevRingRadius * cosf(angle), prevYOffset, prevRingRadius * sinf(angle)});
        glm::vec3 prevBottom = toWorld(bottomCenter + glm::vec3{prevRingRadius * cosf(angle), -prevYOffset, prevRingRadius * sinf(angle)});
        addLine(mesh, prevTop, topOnRing, color, thickness);
        addLine(mesh, prevBottom, bottomOnRing, color, thickness);
      }
    }
  }
}

void Utils::Mesh::addLineCone(Renderer::Mesh &mesh, const glm::vec3 &pos, const glm::vec3 &halfExtend, const glm::vec4 &color, const glm::quat &rot, float thickness)
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
    addLine(mesh, lastBase, nextBase, color, thickness);
    addLine(mesh, tip, lastBase, color, thickness);
    lastBase = nextBase;
  }
}

void Utils::Mesh::addLinePyramid(Renderer::Mesh &mesh, const glm::vec3 &pos, const glm::vec3 &halfExtend, const glm::vec4 &color, const glm::quat &rot, float thickness)
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

  // base square
  addLine(mesh, v0, v1, color, thickness);
  addLine(mesh, v1, v2, color, thickness);
  addLine(mesh, v2, v3, color, thickness);
  addLine(mesh, v3, v0, color, thickness);

  // edges to tip
  addLine(mesh, v0, tip, color, thickness);
  addLine(mesh, v1, tip, color, thickness);
  addLine(mesh, v2, tip, color, thickness);
  addLine(mesh, v3, tip, color, thickness);
}

void Utils::Mesh::addLine(Renderer::Mesh &mesh, const glm::vec3 &start, const glm::vec3 &end, const glm::vec4 &color, float thickness)
{
    // Convert float RGBA (0-1) → u8 RGBA once
    glm::u8vec4 u8Color{
        static_cast<uint8_t>(color.r * 255.0f),
        static_cast<uint8_t>(color.g * 255.0f),
        static_cast<uint8_t>(color.b * 255.0f),
        static_cast<uint8_t>(color.a * 255.0f)
    };

    glm::vec3 dir = end - start;
    float len = glm::length(dir);
    if (len < 0.001f) return;
    dir /= len;

    // First perpendicular
    glm::vec3 perp1 = glm::abs(dir.y) > 0.9f
        ? glm::cross(dir, glm::vec3(1, 0, 0))
        : glm::cross(dir, glm::vec3(0, 1, 0));
    perp1 = glm::normalize(perp1) * (thickness * 0.5f);

    // Second perpendicular (crossed)
    glm::vec3 perp2 = glm::normalize(glm::cross(dir, perp1)) * (thickness * 0.5f);

    uint16_t startIdx = mesh.vertLines.size();

    // Quad 1
    mesh.vertLines.push_back({start + perp1, 0, u8Color});
    mesh.vertLines.push_back({start - perp1, 0, u8Color});
    mesh.vertLines.push_back({end   + perp1, 0, u8Color});
    mesh.vertLines.push_back({end   - perp1, 0, u8Color});

    // Quad 2
    mesh.vertLines.push_back({start + perp2, 0, u8Color});
    mesh.vertLines.push_back({start - perp2, 0, u8Color});
    mesh.vertLines.push_back({end   + perp2, 0, u8Color});
    mesh.vertLines.push_back({end   - perp2, 0, u8Color});

    // Indices for Quad 1
    mesh.indices.push_back(startIdx + 0);
    mesh.indices.push_back(startIdx + 1);
    mesh.indices.push_back(startIdx + 2);

    mesh.indices.push_back(startIdx + 2);
    mesh.indices.push_back(startIdx + 1);
    mesh.indices.push_back(startIdx + 3);

    // Indices for Quad 2
    mesh.indices.push_back(startIdx + 4);
    mesh.indices.push_back(startIdx + 5);
    mesh.indices.push_back(startIdx + 6);

    mesh.indices.push_back(startIdx + 6);
    mesh.indices.push_back(startIdx + 5);
    mesh.indices.push_back(startIdx + 7);
}

void Utils::Mesh::addSprite(Renderer::Mesh &mesh, const glm::vec3 &pos, uint32_t objectId, uint8_t spriteIdx, const glm::vec4 &color)
{
  // Convert float RGBA (0-1) → u8 RGBA for sprite (the alpha channel is used as sprite index)
  glm::u8vec4 col{
      static_cast<uint8_t>(color.r * 255.0f),
      static_cast<uint8_t>(color.g * 255.0f),
      static_cast<uint8_t>(color.b * 255.0f),
      spriteIdx   // keep the sprite index in the alpha byte as before
  };

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