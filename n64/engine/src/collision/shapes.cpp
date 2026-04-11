/**
 * @file shapes.cpp
 * @author Kevin Reier <https://github.com/Byterset>
 * @brief Defines the Properties and helper functions of the different basic Collider shapes (see shapes.h)
 */
#include "collision/shapes.h"

#include <cmath>

using namespace P64::Coll;

namespace {
  constexpr float SQRT_1_2 = 0.707106781f;
}

// ── Box ─────────────────────────────────────────────────────────────

AABB BoxShape::boundingBox(const fm_quat_t *q) const {
  float ex, ey, ez;

  if(!q) {
    ex = halfSize.x; ey = halfSize.y; ez = halfSize.z;
  } else {
    float x = q->x, y = q->y, z = q->z, w = q->w;
    float xx = x*x, yy = y*y, zz = z*z;
    float xy = x*y, xz = x*z, yz = y*z;
    float wx = w*x, wy = w*y, wz = w*z;

    float r00 = 1 - 2*(yy+zz), r01 = 2*(xy-wz), r02 = 2*(xz+wy);
    float r10 = 2*(xy+wz), r11 = 1 - 2*(xx+zz), r12 = 2*(yz-wx);
    float r20 = 2*(xz-wy), r21 = 2*(yz+wx), r22 = 1 - 2*(xx+yy);

    ex = halfSize.x*fabsf(r00) + halfSize.y*fabsf(r01) + halfSize.z*fabsf(r02);
    ey = halfSize.x*fabsf(r10) + halfSize.y*fabsf(r11) + halfSize.z*fabsf(r12);
    ez = halfSize.x*fabsf(r20) + halfSize.y*fabsf(r21) + halfSize.z*fabsf(r22);
  }

  return {fm_vec3_t{{-ex, -ey, -ez}}, fm_vec3_t{{ex, ey, ez}}};
}

fm_vec3_t BoxShape::inertiaTensor(float mass) const {
  float hxSq = halfSize.x * halfSize.x;
  float hySq = halfSize.y * halfSize.y;
  float hzSq = halfSize.z * halfSize.z;
  float scale = mass / 3.0f;

  return fm_vec3_t{{
    scale * (hySq + hzSq),
    scale * (hxSq + hzSq),
    scale * (hxSq + hySq)
  }};
}

// ── Capsule ─────────────────────────────────────────────────────────

AABB CapsuleShape::boundingBox(const fm_quat_t *q) const {
  fm_vec3_t r{};

  if(!q) {
    r = fm_vec3_t{{0.0f, innerHalfHeight, 0.0f}};
  } else {
    float x = q->x, y = q->y, z = q->z, w = q->w;
    float xx = x*x, yy = y*y, zz = z*z;
    float xy = x*y, xz = x*z, yz = y*z;
    float wx = w*x, wy = w*y, wz = w*z;

    float r01 = 2*(xy-wz);
    float r11 = 1 - 2*(xx+zz);
    float r21 = 2*(yz+wx);

    r.x = r01 * innerHalfHeight;
    r.y = r11 * innerHalfHeight;
    r.z = r21 * innerHalfHeight;
  }

  float absX = fabsf(r.x);
  float absY = fabsf(r.y);
  float absZ = fabsf(r.z);

  return {
    fm_vec3_t{{-absX - radius, -absY - radius, -absZ - radius}},
    fm_vec3_t{{ absX + radius,  absY + radius,  absZ + radius}}
  };
}

fm_vec3_t CapsuleShape::inertiaTensor(float mass) const {
  float cylHeight = 2.0f * innerHalfHeight;

  constexpr float PI = 3.14159265358979f;

  float cylVol = PI * radius * radius * cylHeight;
  float sphereVol = (4.0f / 3.0f) * PI * radius * radius * radius;
  float totalVol = cylVol + sphereVol;

  float cylMass = mass * (cylVol / totalVol);
  float sphereMass = mass * (sphereVol / totalVol);

  float rSq = radius * radius;
  float hSq = cylHeight * cylHeight;
  float cylPerp = cylMass * (3.0f * rSq + hSq) / 12.0f;
  float cylAxial = 0.5f * cylMass * rSq;

  float sphereInertia = 0.4f * sphereMass * rSq;
  float offsetSq = innerHalfHeight * innerHalfHeight;
  float hemiMass = sphereMass * 0.5f;
  float spherePerp = sphereInertia + 2.0f * hemiMass * offsetSq;

  return fm_vec3_t{{
    cylPerp + spherePerp,
    cylAxial + sphereInertia,
    cylPerp + spherePerp
  }};
}

// ── Cylinder ────────────────────────────────────────────────────────

fm_vec3_t CylinderShape::support(const fm_vec3_t &dir) const {
  float x = dir.x;
  float y = dir.y;
  float z = dir.z;

  fm_vec3_t output;
  output.y = copysignf(halfHeight, y);

  float absX = fabsf(x);
  float absZ = fabsf(z);

  if(absX < SQRT_1_2 * (absX + absZ) && absZ < SQRT_1_2 * (absX + absZ)) {
    output.x = (x >= 0.0f) ? radius * SQRT_1_2 : -radius * SQRT_1_2;
    output.z = (z >= 0.0f) ? radius * SQRT_1_2 : -radius * SQRT_1_2;
  } else if(absX > absZ) {
    output.x = (x >= 0.0f) ? radius : -radius;
    output.z = 0.0f;
  } else {
    output.x = 0.0f;
    output.z = (z >= 0.0f) ? radius : -radius;
  }

  return output;
}

AABB CylinderShape::boundingBox(const fm_quat_t *q) const {
  float ex = radius, ey = halfHeight, ez = radius;

  if(!q) {
    return {fm_vec3_t{{-ex, -ey, -ez}}, fm_vec3_t{{ex, ey, ez}}};
  }

  float x = q->x, y = q->y, z = q->z, w = q->w;
  float xx = x*x, yy = y*y, zz = z*z;
  float xy = x*y, xz = x*z, yz = y*z;
  float wx = w*x, wy = w*y, wz = w*z;

  float r00 = 1.0f - 2.0f*(yy+zz);
  float r01 =        2.0f*(xy-wz);
  float r02 =        2.0f*(xz+wy);
  float r10 =        2.0f*(xy+wz);
  float r11 = 1.0f - 2.0f*(xx+zz);
  float r12 =        2.0f*(yz-wx);
  float r20 =        2.0f*(xz-wy);
  float r21 =        2.0f*(yz+wx);
  float r22 = 1.0f - 2.0f*(xx+yy);

  float wxe = fabsf(r00)*ex + fabsf(r01)*ey + fabsf(r02)*ez;
  float wye = fabsf(r10)*ex + fabsf(r11)*ey + fabsf(r12)*ez;
  float wze = fabsf(r20)*ex + fabsf(r21)*ey + fabsf(r22)*ez;

  return {fm_vec3_t{{-wxe, -wye, -wze}}, fm_vec3_t{{wxe, wye, wze}}};
}

fm_vec3_t CylinderShape::inertiaTensor(float mass) const {
  float h = 2.0f * halfHeight;
  float rSq = radius * radius;
  float hSq = h * h;

  float perpInertia = mass * (3.0f * rSq + hSq) / 12.0f;
  float axialInertia = 0.5f * mass * rSq;

  return fm_vec3_t{{perpInertia, axialInertia, perpInertia}};
}

// ── Cone ────────────────────────────────────────────────────────────

fm_vec3_t ConeShape::support(const fm_vec3_t &dir) const {
  float dx = dir.x;
  float dy = dir.y;
  float dz = dir.z;

  float sinAlpha = radius / sqrtf(radius * radius + 4.0f * halfHeight * halfHeight);
  float sin2 = sinAlpha * sinAlpha;
  float sigma2 = dx * dx + dz * dz;
  float dy2 = dy * dy;
  float d2 = dy2 + sigma2;

  if(dy > 0.0f && dy2 > d2 * sin2) {
    return fm_vec3_t{{0.0f, halfHeight, 0.0f}};
  }

  if(sigma2 > 0.0f) {
    float invSigma = 1.0f / sqrtf(sigma2);
    return fm_vec3_t{{radius * dx * invSigma, -halfHeight, radius * dz * invSigma}};
  }

  return fm_vec3_t{{0.0f, -halfHeight, 0.0f}};
}

AABB ConeShape::boundingBox(const fm_quat_t *q) const {
  if(!q) {
    return {fm_vec3_t{{-radius, -halfHeight, -radius}}, fm_vec3_t{{radius, halfHeight, radius}}};
  }

  float x = q->x, y = q->y, z = q->z, w = q->w;
  float xx = x*x, yy = y*y, zz = z*z;
  float xy = x*y, xz = x*z, yz = y*z;
  float wx = w*x, wy = w*y, wz = w*z;

  float r00 = 1 - 2*(yy+zz), r01 = 2*(xy-wz), r02 = 2*(xz+wy);
  float r10 = 2*(xy+wz), r11 = 1 - 2*(xx+zz), r12 = 2*(yz-wx);
  float r20 = 2*(xz-wy), r21 = 2*(yz+wx), r22 = 1 - 2*(xx+yy);

  auto rotate = [&](float px, float py, float pz) -> fm_vec3_t {
    return fm_vec3_t{{
      r00*px + r01*py + r02*pz,
      r10*px + r11*py + r12*pz,
      r20*px + r21*py + r22*pz
    }};
  };

  auto apex = rotate(0.0f, halfHeight, 0.0f);
  fm_vec3_t bmin = apex, bmax = apex;

  fm_vec3_t basePts[4] = {
    rotate( radius, -halfHeight,  0.0f),
    rotate(-radius, -halfHeight,  0.0f),
    rotate( 0.0f,   -halfHeight,  radius),
    rotate( 0.0f,   -halfHeight, -radius)
  };

  for(int i = 0; i < 4; ++i) {
    bmin = vec3Min(bmin, basePts[i]);
    bmax = vec3Max(bmax, basePts[i]);
  }

  return {bmin, bmax};
}

fm_vec3_t ConeShape::inertiaTensor(float mass) const {
  float h = 2.0f * halfHeight;
  float rSq = radius * radius;
  float hSq = h * h;

  float perpInertia = (3.0f / 80.0f) * mass * (4.0f * rSq + hSq);
  float axialInertia = 0.3f * mass * rSq;

  return fm_vec3_t{{perpInertia, axialInertia, perpInertia}};
}

// ── Pyramid ─────────────────────────────────────────────────────────

fm_vec3_t PyramidShape::support(const fm_vec3_t &dir) const {
  float apexDot = halfHeight * dir.y;
  float baseDot = fabsf(dir.x) * baseHalfWidthX + fabsf(dir.z) * baseHalfWidthZ - halfHeight * dir.y;

  if(apexDot > baseDot) {
    return fm_vec3_t{{0.0f, halfHeight, 0.0f}};
  }

  return fm_vec3_t{{
    copysignf(baseHalfWidthX, dir.x),
    -halfHeight,
    copysignf(baseHalfWidthZ, dir.z)
  }};
}

AABB PyramidShape::boundingBox(const fm_quat_t *q) const {
  float ex, ey, ez;

  if(!q) {
    ex = baseHalfWidthX; ey = halfHeight; ez = baseHalfWidthZ;
  } else {
    float x = q->x, y = q->y, z = q->z, w = q->w;
    float xx = x*x, yy = y*y, zz = z*z;
    float xy = x*y, xz = x*z, yz = y*z;
    float wx = w*x, wy = w*y, wz = w*z;

    float r00 = 1 - 2*(yy+zz), r01 = 2*(xy-wz), r02 = 2*(xz+wy);
    float r10 = 2*(xy+wz), r11 = 1 - 2*(xx+zz), r12 = 2*(yz-wx);
    float r20 = 2*(xz-wy), r21 = 2*(yz+wx), r22 = 1 - 2*(xx+yy);

    ex = baseHalfWidthX*fabsf(r00) + halfHeight*fabsf(r01) + baseHalfWidthZ*fabsf(r02);
    ey = baseHalfWidthX*fabsf(r10) + halfHeight*fabsf(r11) + baseHalfWidthZ*fabsf(r12);
    ez = baseHalfWidthX*fabsf(r20) + halfHeight*fabsf(r21) + baseHalfWidthZ*fabsf(r22);
  }

  return {fm_vec3_t{{-ex, -ey, -ez}}, fm_vec3_t{{ex, ey, ez}}};
}

fm_vec3_t PyramidShape::inertiaTensor(float mass) const {
  float mDiv20 = mass * 0.05f;
  float mDiv5 = mass * 0.2f;

  float Ixx = (mDiv5 * baseHalfWidthZ * baseHalfWidthZ) + (mDiv20 * 3.0f * halfHeight * halfHeight);
  float Iyy = (mDiv5 * baseHalfWidthX * baseHalfWidthX) + (mDiv5 * baseHalfWidthZ * baseHalfWidthZ);
  float Izz = (mDiv5 * baseHalfWidthX * baseHalfWidthX) + (mDiv20 * 3.0f * halfHeight * halfHeight);

  return fm_vec3_t{{Ixx, Iyy, Izz}};
}
