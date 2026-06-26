/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "camera.h"

#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/geometric.hpp"

namespace
{
  constexpr glm::vec3 WORLD_UP{0,1,0};
  constexpr glm::vec3 WORLD_FORWARD{0,0,-1};
  constexpr float ORTHO_SIZE = 310.0f;
  // Framing for the "focus selection" action
  constexpr float AABB_FOCUS_MARGIN = 1.25f;     // extra space around a real bounding box
  constexpr float NO_VOLUME_FOCUS_DIST = 220.0f; // overview distance for volume-less objects
}

Renderer::Camera::Camera() {
  rot = glm::rotate(
    glm::identity<glm::quat>(),
    glm::radians(-180.0f),
    glm::vec3(1,0,0)
  );
  focus(glm::vec3(0,220,0), 220);
}

void Renderer::Camera::update() {
  pos += velocity;
  pivot += velocity;
  velocity *= 0.9f;
  
  if (fabs(zoomSpeed) < 0.01) {
    zoomSpeed = 0.0f;
    return;
  }

  // Zoom moves the camera along its view axis toward/away from the (fixed) pivot, so the pivot
  // stays the orbit centre. Clamp zoom-IN to a minimum distance so it can't reach or pass through
  // the centre. (The old code translated the pivot forward when close, dollying past the centre.)
  constexpr float MIN_PIVOT_DIST = 2.0f;
  glm::vec3 fwd = rot * WORLD_FORWARD; // unit, points from the camera toward the pivot
  float camDist = glm::length(pos - pivot);
  float step = zoomSpeed; // >0 zoom in (toward pivot), <0 zoom out (away)
  if (step > 0.0f) step = glm::min(step, glm::max(0.0f, camDist - MIN_PIVOT_DIST));
  pos += fwd * step;
  zoomSpeed *= 0.9f;
}

void Renderer::Camera::apply(UniformGlobal &uniGlobal)
{
  float aspect = screenSize.x / screenSize.y;
  float near = 10.0f;
  float far = 90'000.0f;
  float fovRad = glm::radians(fov);

  if(isOrtho)
  {
    uniGlobal.spriteSize = {10, 10};
    float orthoSize = ORTHO_SIZE;
    uniGlobal.projMat = glm::ortho(
      -orthoSize * aspect,
      orthoSize * aspect,
      -orthoSize,
      orthoSize,
      -far, far
    );
  } else
  {
    uniGlobal.spriteSize = {7000, 7000};
    uniGlobal.projMat = glm::perspective(fovRad, aspect, near, far);
  }
  uniGlobal.spriteSize *= ctx.prefs.renderFactorAA;

  const glm::vec3 direction = glm::normalize(rot * WORLD_FORWARD);
  const glm::vec3 dynamicUp = glm::normalize(rot * WORLD_UP);
  const glm::vec3 target = pos + direction;
  uniGlobal.cameraMat = glm::lookAt(pos, target, dynamicUp);
}

void Renderer::Camera::rotateDelta(glm::vec2 screenDelta)
{
  if (!isRotating) {
    rotBase = rot;
    isRotating = true;
  }

  constexpr float rotSpeed = 0.0025f;
  // rotate based on screen delta
  float angleX = screenDelta.x * -rotSpeed;
  float angleY = screenDelta.y * -rotSpeed;
  glm::quat qx = glm::angleAxis(angleX, glm::vec3(0, 1, 0));
  glm::quat qy = glm::angleAxis(angleY, glm::vec3(1, 0, 0));
  rot = qx * rotBase * qy;
  rot = glm::normalize(rot);

}

void Renderer::Camera::lookDelta(glm::vec2 screenDelta)
{
  if (!isRotating) {
    pivotBase = pivot;
  }

  rotateDelta(screenDelta);
  glm::vec3 diff = pos - pivotBase;
  pivot = pos - rot * glm::inverse(rotBase) * diff;
}

void Renderer::Camera::orbitDelta(glm::vec2 screenDelta)
{
  if (!isRotating) {
    posBase = pos;
  }

  rotateDelta(screenDelta);
  glm::vec3 diff = posBase - pivot;
  pos = pivot + rot * glm::inverse(rotBase) * diff;
}

void Renderer::Camera::moveDelta(glm::vec2 screenDelta) {
  if (!isMoving) {
    posBase = pos;
    isMoving = true;
  }

  float pixelsToWorld = 0.001f;
  if (isOrtho) {
    if (screenSize.y > 0.0f) {
      pixelsToWorld = (ORTHO_SIZE * 2.0f) / screenSize.y;
    }
  } else {
    float dist = glm::length(pivot - pos);
    if (dist > 0.001f) {
      pixelsToWorld = dist * 0.001f;
    }
  }

  float moveX = screenDelta.x * pixelsToWorld;
  float moveY = screenDelta.y * -pixelsToWorld;

  glm::vec3 right = rot * glm::vec3(1, 0, 0);
  glm::vec3 up = rot * glm::vec3(0, 1, 0);
 
  glm::vec3 diff = pivot - pos;
  pos = posBase + (right * moveX) + (up * moveY);
  pivot = pos + diff;
}

void Renderer::Camera::focus(glm::vec3 position, float distance) {
  isMoving = false;
  isRotating = false;
  pivot = position;
  glm::vec3 posOffset = rot * -WORLD_FORWARD * distance;
  pos = pivot + posOffset;
}

void Renderer::Camera::focusSelection(Context &ctx) {
  const auto& selectedUUIDs = ctx.getSelectedObjectUUIDs();
  if (selectedUUIDs.empty()) return;
  
  auto scene = ctx.project->getScenes().getLoadedScene();
  if (!scene) return;

  Utils::AABB aabb{};
  bool hasVolume = false;
  for (uint32_t uuid : selectedUUIDs) {
    auto obj = scene->getObjectByUUID(uuid);
    if (!obj) continue;

    bool objHasVolume = false;
    Utils::AABB objAABB = obj->getWorldAABB(&objHasVolume);
    hasVolume |= objHasVolume;
    aabb.addPoint(objAABB.min);
    aabb.addPoint(objAABB.max);
  }

  // Objects with no real bounding volume (no mesh/collider) have no meaningful size
  // to frame, so fitting their tiny fallback box zooms in absurdly close. Instead pull
  // back to a comfortable overview distance so the object and its surroundings are visible.
  if (!hasVolume) {
    focus(aabb.getCenter(), NO_VOLUME_FOCUS_DIST);
    return;
  }

  // Project the half-extent onto the camera's local axes so the whole AABB is framed
  // regardless of viewing angle, fitting both the horizontal and vertical field of view.
  glm::vec3 h = aabb.getHalfExtend();
  glm::vec3 right = rot * glm::vec3{1, 0, 0};
  glm::vec3 up    = rot * glm::vec3{0, 1, 0};
  glm::vec3 fwd   = rot * glm::vec3{0, 0, 1};
  float width  = glm::abs(right.x)*h.x + glm::abs(right.y)*h.y + glm::abs(right.z)*h.z;
  float height = glm::abs(up.x)*h.x    + glm::abs(up.y)*h.y    + glm::abs(up.z)*h.z;
  float depth  = glm::abs(fwd.x)*h.x   + glm::abs(fwd.y)*h.y   + glm::abs(fwd.z)*h.z;

  float aspect = (screenSize.y > 0.0f) ? (screenSize.x / screenSize.y) : 1.0f;
  float fovY = glm::radians(fov);
  float tanY = tanf(fovY * 0.5f);
  float tanX = tanY * aspect;

  // distance needed for each axis to fit inside the frustum, plus the object's own depth
  float distForHeight = height / tanY;
  float distForWidth  = width / tanX;
  float dist = depth + glm::max(distForHeight, distForWidth);
  dist *= AABB_FOCUS_MARGIN; // a little breathing room around the AABB
  focus(aabb.getCenter(), dist);
}