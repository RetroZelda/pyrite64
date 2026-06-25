/**
* @copyright 2024 - Max Bebök
* @license MIT
*/
#include "scene/camera.h"
#include "lib/logger.h"
#include "scene/globalState.h"

#include <cmath>

namespace
{
  constexpr fm_vec3_t FORWARD{0,0,-1};
  constexpr fm_vec3_t UP{0,1,0};
}

P64::Camera::Camera() {
  viewports = t3d_viewport_create_buffered(3);
}

P64::Camera::~Camera() {
  t3d_viewport_destroy(&viewports);
}

void P64::Camera::update([[maybe_unused]] float deltaTime)
{
  t3d_viewport_set_perspective(&viewports, fov, aspectRatio, near, far);
  t3d_viewport_set_view_matrix(&viewports, &viewMatrix);
}

void P64::Camera::attach() {
  t3d_viewport_attach(viewports);
}

void P64::Camera::setScreenArea(int x, int y, int width, int height) {
  t3d_viewport_set_area(viewports, x,y, width, height);
}

void P64::Camera::setLookAt(const fm_vec3_t &newPos, const fm_vec3_t &newTarget, const fm_vec3_t &newUp) {
  // OUT-OF-RANGE guard: t3d's view matrix is 16.16 fixed-point, so the translation row
  // (= -dot(basis, eye)) overflows int32 in t3d_mat4_to_fixed once |eye| approaches ~32768 —
  // a hard FP-cast-overflow crash. A misbehaving camera (e.g. a follow script that flings the
  // camera far from origin on its first frame) must NOT take down the whole game, so clamp the
  // eye into range and warn. The level itself stays put; only the bad camera position is reined in.
  fm_vec3_t clampedPos = newPos;
  {
    float l2 = clampedPos.x*clampedPos.x + clampedPos.y*clampedPos.y + clampedPos.z*clampedPos.z;
    constexpr float SAFE = 30000.0f;
    if(l2 > SAFE*SAFE) {
      static bool warned = false;
      if(!warned) {
        Log::warn("Camera position out of t3d fixed-point range; clamping (check your camera script)");
        warned = true;
      }
      float s = SAFE / sqrtf(l2);
      clampedPos.x *= s; clampedPos.y *= s; clampedPos.z *= s;
    }
  }

  pos = clampedPos;
  fm_vec3_t safeTarget = newTarget;
  fm_vec3_t safeUp = newUp;

  // Guard a DEGENERATE look-at. t3d_mat4_look_at() normalizes cross(forward, up) with no
  // protection: if forward is ~0 (eye≈target) or forward is parallel to up, that cross is ~0
  // and the normalize blows the view matrix up to values that overflow t3d's 16.16 fixed-point
  // conversion -> a hard FP-cast-overflow crash in t3d_mat4_to_fixed. This happens e.g. on a
  // follow/boom camera's first frame, before it has positioned away from its target.
  float fx = safeTarget.x - clampedPos.x, fy = safeTarget.y - clampedPos.y, fz = safeTarget.z - clampedPos.z;
  if(fx*fx + fy*fy + fz*fz < 1e-3f) {            // eye ≈ target -> look toward -Z
    safeTarget = clampedPos; safeTarget.z -= 1.0f;
    fx = 0.0f; fy = 0.0f; fz = -1.0f;
  }
  if(safeUp.x*safeUp.x + safeUp.y*safeUp.y + safeUp.z*safeUp.z < 1e-6f) { // up ~0 -> +Y
    safeUp.x = 0.0f; safeUp.y = 1.0f; safeUp.z = 0.0f;
  }
  // forward parallel to up?  cross(normalize(forward), up) ~ 0
  float inv = 1.0f / sqrtf(fx*fx + fy*fy + fz*fz);
  float nx = fx*inv, ny = fy*inv, nz = fz*inv;
  float cx = ny*safeUp.z - nz*safeUp.y;
  float cy = nz*safeUp.x - nx*safeUp.z;
  float cz = nx*safeUp.y - ny*safeUp.x;
  if(cx*cx + cy*cy + cz*cz < 1e-4f) {            // parallel -> pick a non-parallel up
    if(fabsf(ny) > 0.9f) { safeUp.x = 0.0f; safeUp.y = 0.0f; safeUp.z = -1.0f; }
    else                 { safeUp.x = 0.0f; safeUp.y = 1.0f; safeUp.z = 0.0f; }
  }

  target = safeTarget;
  up = safeUp;
  t3d_mat4_look_at(&viewMatrix, &clampedPos, &safeTarget, &safeUp);
  needsProjUpdate = true;
}

void P64::Camera::setPosRot(const fm_vec3_t &newPos, const fm_quat_t&rot) {
  setLookAt(newPos, newPos + (rot * FORWARD), rot * UP);
}

fm_vec3_t P64::Camera::getScreenPos(const fm_vec3_t &worldPos)
{
  fm_vec3_t res{};
  t3d_viewport_calc_viewspace_pos(viewports, res, worldPos);
  return res;
}

