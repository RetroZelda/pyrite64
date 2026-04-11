/**
* @copyright 2024 - Max Bebök
* @license MIT
*/
#pragma once
#include <libdragon.h>

namespace Debug
{
  void init();

  void drawAABB(const fm_vec3_t &p, const fm_vec3_t &halfExtend, color_t color = {0xFF, 0xFF, 0xFF, 0xFF});
  void drawLine(const fm_vec3_t &a, const fm_vec3_t &b, color_t color = {0xFF,0xFF,0xFF,0xFF});
  color_t paletteColor(uint32_t index);
  void drawSphere(const fm_vec3_t &center, float radius, color_t color = {0xFF,0xFF,0xFF,0xFF});
  void drawOBB(const fm_vec3_t &p, const fm_vec3_t &halfExtend, const fm_quat_t &rot, color_t color = {0xFF,0xFF,0xFF,0xFF});
  void drawCapsule(const fm_vec3_t &p, float radius, float innerHalfHeight, const fm_quat_t &rot, color_t color = {0xFF,0xFF,0xFF,0xFF});
  void drawCylinder(const fm_vec3_t &p, float radius, float halfHeight, const fm_quat_t &rot, color_t color = {0xFF,0xFF,0xFF,0xFF});
  void drawCone(const fm_vec3_t &p, float radius, float halfHeight, const fm_quat_t &rot, color_t color = {0xFF,0xFF,0xFF,0xFF});
  void drawPyramid(const fm_vec3_t &p, float baseHalfWidthX, float baseHalfWidthZ, float halfHeight, const fm_quat_t &rot, color_t color = {0xFF,0xFF,0xFF,0xFF});

  void draw(surface_t *fb);

  void printStart();
  float print(float x, float y, const char* str);
  float printf(float x, float y, const char *fmt, ...);

  void destroy();
}