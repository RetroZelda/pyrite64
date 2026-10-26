/**
* @copyright 2026 - Max Bebök
* @license MIT
*
* Shared, dependency-free spline math for the Spline component (id 14).
*
* SINGLE SOURCE OF TRUTH for the curve a Spline describes. Included by BOTH the editor
* (viewport preview, point editor, scene build) and the N64 runtime (P64::Comp::Spline), so the
* polyline guards walk on hardware is the one drawn in the editor. Rules (as particleSim.h):
*  - only + - * / and sqrt (IEEE correctly rounded on both platforms), no pow/sin/cos,
*  - no libdragon / glm / engine types, storage owned by the caller,
*  - clean under the engine flags (-Wall -Wextra -Werror -Wshadow -Wdouble-promotion).
*
* Component payload v1 (editor Build -> engine initData), big-endian, starts 4-byte aligned:
*   +0  u8   version     FORMAT_VERSION (1)
*   +1  u8   flags       FLAG_CLOSED | FLAG_WORLD (bits 2..7 written as 0, ignored on read)
*   +2  u8   type        TYPE_LINEAR / TYPE_CATMULL_ROM (anything else reads as LINEAR)
*   +3  u8   subdiv      samples per segment (1..MAX_SUBDIV, 1 for LINEAR)
*   +4  u8   pointCount  0..MAX_POINTS (larger values are clamped)
*   +5  u8   reserved    0
*   +6  u16  pathId      0 = none
*   +8  f32  points[pointCount][3]   world space if FLAG_WORLD, else object-local
*   +8+12N u8 waitDs[pointCount]     wait at each point in deciseconds (0..255)
*   (zero padding to 4 bytes is added by the scene builder)
*/
#pragma once
#include <cstdint>
#include <cmath>

namespace P64::SplineMath
{
  struct Vec3   { float x{}, y{}, z{}; };
  struct Sample { float x{}, y{}, z{}, s{}; }; // polyline vertex + cumulative arc length

  // --- format ----------------------------------------------------------------------------------
  constexpr uint8_t  FORMAT_VERSION       = 1;
  constexpr uint8_t  FLAG_CLOSED          = 1 << 0; // loop back from the last point to the first
  constexpr uint8_t  FLAG_WORLD           = 1 << 1; // points already in world space (scene builds)
  constexpr uint8_t  FLAG_MASK            = FLAG_CLOSED | FLAG_WORLD;
  constexpr uint8_t  TYPE_LINEAR          = 0;      // straight lines between points
  constexpr uint8_t  TYPE_CATMULL_ROM     = 1;      // centripetal Catmull-Rom through every point
  constexpr uint32_t OFF_VERSION = 0, OFF_FLAGS = 1, OFF_TYPE = 2, OFF_SUBDIV = 3,
                     OFF_COUNT = 4, OFF_RESERVED = 5, OFF_PATH_ID = 6, OFF_POINTS = 8;
  constexpr uint32_t PAYLOAD_HEADER_SIZE  = 8;
  constexpr uint32_t MAX_POINTS           = 64;
  constexpr uint32_t MAX_SUBDIV           = 8;
  constexpr uint32_t DEFAULT_SUBDIV       = 6;
  constexpr uint32_t MAX_SAMPLES          = MAX_POINTS * MAX_SUBDIV + 1; // 513
  constexpr uint32_t MAX_RECORD_BYTES     = 255u * 4u;  // u8 word count incl. the 4-byte record header
  constexpr uint32_t RUNTIME_HEADER_BYTES = 24;         // == sizeof(P64::Comp::Spline) (static_assert there)

  constexpr uint32_t payloadBytes(uint32_t count) { return PAYLOAD_HEADER_SIZE + 13u * count; }
  constexpr uint32_t recordBytes(uint32_t count)  { return 4u + ((payloadBytes(count) + 3u) & ~3u); }
  static_assert(recordBytes(MAX_POINTS) <= MAX_RECORD_BYTES, "spline record must fit the u8 word count");

  // --- vector helpers ---------------------------------------------------------------------------
  inline Vec3  add(Vec3 a, Vec3 b)     { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
  inline Vec3  sub(Vec3 a, Vec3 b)     { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
  inline Vec3  mul(Vec3 a, float f)    { return {a.x * f, a.y * f, a.z * f}; }
  inline float dot(Vec3 a, Vec3 b)     { return a.x * b.x + a.y * b.y + a.z * b.z; }
  inline float lenSq(Vec3 a)           { return dot(a, a); }
  inline Vec3  toVec3(const Sample &a) { return {a.x, a.y, a.z}; }

  // --- counts (pure; the engine sizes its allocation from these) -------------------------------
  inline uint32_t clampCount(uint32_t count) { return count > MAX_POINTS ? MAX_POINTS : count; }
  inline bool isClosed(uint32_t count, bool closed) { return closed && count >= 3; }
  inline uint32_t effectiveSubdiv(uint32_t type, uint32_t subdiv)
  {
    if(type != TYPE_CATMULL_ROM) return 1;
    if(subdiv < 1) return 1;
    return subdiv > MAX_SUBDIV ? MAX_SUBDIV : subdiv;
  }
  inline uint32_t segmentCount(uint32_t count, bool closed)
  {
    if(count < 2) return 0;
    return isClosed(count, closed) ? count : count - 1;
  }
  inline uint32_t sampleCount(uint32_t count, bool closed, uint32_t type, uint32_t subdiv)
  {
    count = clampCount(count);
    if(count == 0) return 0;
    return segmentCount(count, closed) * effectiveSubdiv(type, subdiv) + 1;
  }
  // bytes the engine component allocates (header + samples + waits rounded to 4)
  inline uint32_t runtimeBytes(uint32_t count, bool closed, uint32_t type, uint32_t subdiv)
  {
    const uint32_t n = clampCount(count);
    return RUNTIME_HEADER_BYTES + sampleCount(n, closed, type, subdiv) * (uint32_t)sizeof(Sample) + ((n + 3u) & ~3u);
  }
  // sample index holding control point `idx` exactly (closed: idx == count is point 0 at the end)
  inline uint32_t pointSampleIndex(uint32_t idx, uint32_t subdivEff) { return idx * subdivEff; }
  // polyline piece `sampleIdx` (sample -> sample+1) at fraction t -> curve segment + parameter u
  inline void sampleToSegment(uint32_t sampleIdx, float t, uint32_t subdivEff, uint32_t &outSeg, float &outU)
  {
    outSeg = sampleIdx / subdivEff;
    outU = ((float)(sampleIdx % subdivEff) + t) / (float)subdivEff;
  }

  // --- curve evaluation -------------------------------------------------------------------------
  // Control point `idx` (count >= 2). Closed curves wrap; open curves mirror a phantom point past
  // each end, which gives the end segments a straight-continuation tangent.
  inline Vec3 pointAt(const Vec3* pts, uint32_t count, bool closed, int32_t idx)
  {
    const int32_t n = (int32_t)count;
    if(closed) {
      idx %= n;
      if(idx < 0) idx += n;
      return pts[idx];
    }
    if(idx < 0)  return sub(mul(pts[0], 2.0f), pts[1]);
    if(idx >= n) return sub(mul(pts[n - 1], 2.0f), pts[n - 2]);
    return pts[idx];
  }

  // centripetal knot step |b - a|^0.5
  inline float knotStep(Vec3 a, Vec3 b) { return std::sqrt(std::sqrt(lenSq(sub(b, a)))); }

  // Position on segment `seg` (point seg -> seg+1) at u in [0,1]. `closed` MUST be the effective
  // flag (isClosed()). Degenerate policy: a zero-length chord is linear; a duplicated neighbour
  // reuses the middle knot step (no bulge, no NaN).
  inline Vec3 evalSegment(const Vec3* pts, uint32_t count, bool closed, uint32_t type, uint32_t seg, float u)
  {
    const int32_t i = (int32_t)seg;
    const Vec3 p1 = pointAt(pts, count, closed, i);
    const Vec3 p2 = pointAt(pts, count, closed, i + 1);
    const float d12 = knotStep(p1, p2);
    if(type != TYPE_CATMULL_ROM || d12 < 1e-3f) {
      return add(p1, mul(sub(p2, p1), u));
    }
    const Vec3 p0 = pointAt(pts, count, closed, i - 1);
    const Vec3 p3 = pointAt(pts, count, closed, i + 2);
    float d01 = knotStep(p0, p1);
    float d23 = knotStep(p2, p3);
    if(d01 < 1e-3f) d01 = d12;
    if(d23 < 1e-3f) d23 = d12;

    // non-uniform Catmull-Rom tangents, rescaled to the [0,1] segment parameter
    Vec3 m1 = add(sub(mul(sub(p1, p0), 1.0f / d01), mul(sub(p2, p0), 1.0f / (d01 + d12))), mul(sub(p2, p1), 1.0f / d12));
    Vec3 m2 = add(sub(mul(sub(p2, p1), 1.0f / d12), mul(sub(p3, p1), 1.0f / (d12 + d23))), mul(sub(p3, p2), 1.0f / d23));
    m1 = mul(m1, d12);
    m2 = mul(m2, d12);

    const float u2 = u * u;
    const float u3 = u2 * u;
    const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
    const float h10 = u3 - 2.0f * u2 + u;
    const float h01 = 3.0f * u2 - 2.0f * u3;
    const float h11 = u3 - u2;
    return add(add(mul(p1, h00), mul(m1, h10)), add(mul(p2, h01), mul(m2, h11)));
  }

  // THE tessellation. Writes sampleCount() entries to `out`. Sample pointSampleIndex(i) is control
  // point i bit-exactly; the last sample of a closed curve is point 0. Returns samples written.
  inline uint32_t tessellate(const Vec3* pts, uint32_t count, bool closed, uint32_t type, uint32_t subdiv, Sample* out)
  {
    count = clampCount(count);
    if(count == 0) return 0;
    const bool loop = isClosed(count, closed);
    const uint32_t segs = segmentCount(count, loop);
    const uint32_t steps = effectiveSubdiv(type, subdiv);

    Vec3 prev = pts[0];
    float dist = 0.0f;
    out[0] = {prev.x, prev.y, prev.z, 0.0f};
    uint32_t written = 1;
    for(uint32_t seg = 0; seg < segs; ++seg) {
      for(uint32_t k = 1; k <= steps; ++k) {
        const Vec3 p = (k == steps)
          ? pointAt(pts, count, loop, (int32_t)seg + 1)
          : evalSegment(pts, count, loop, type, seg, (float)k / (float)steps);
        dist += std::sqrt(lenSq(sub(p, prev)));
        out[written++] = {p.x, p.y, p.z, dist};
        prev = p;
      }
    }
    return written;
  }

  // --- arc-length queries on a tessellated polyline (n samples, len = smp[n-1].s) --------------
  inline float wrapS(float s, float len, bool closed)
  {
    if(len <= 0.0f) return 0.0f;
    if(!closed) return s < 0.0f ? 0.0f : (s > len ? len : s);
    s = std::fmod(s, len);
    if(s < 0.0f) s += len;
    return s < len ? s : 0.0f;
  }

  // Largest i in [0, n-2] with smp[i].s <= s (n >= 2).
  inline uint32_t segmentAt(const Sample* smp, uint32_t n, float s)
  {
    uint32_t lo = 0;
    uint32_t hi = n - 1;
    while(hi - lo > 1) {
      const uint32_t mid = (lo + hi) / 2;
      if(smp[mid].s <= s) lo = mid; else hi = mid;
    }
    return lo;
  }

  inline Vec3 positionAt(const Sample* smp, uint32_t n, bool closed, float s)
  {
    if(n == 0) return {};
    if(n == 1) return toVec3(smp[0]);
    s = wrapS(s, smp[n - 1].s, closed);
    const uint32_t i = segmentAt(smp, n, s);
    const Sample &a = smp[i];
    const Sample &b = smp[i + 1];
    const float segLen = b.s - a.s;
    const float t = segLen > 1e-6f ? (s - a.s) / segLen : 0.0f;
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
  }

  // Unit direction of increasing s at s; {0,0,1} when every piece has zero length. A zero-length
  // piece (duplicated points) takes the next real piece ahead: a closed curve continues across the
  // seam, the end of an open curve takes the last real piece before s (duplicated end points).
  inline Vec3 directionAt(const Sample* smp, uint32_t n, bool closed, float s)
  {
    if(n < 2) return {0.0f, 0.0f, 1.0f};
    s = wrapS(s, smp[n - 1].s, closed);
    const uint32_t start = segmentAt(smp, n, s);
    for(uint32_t k = 0; k < n - 1; ++k) {
      // pieces start, start+1, ... then past the last one: closed 0, 1, ... / open start-1, start-2, ...
      uint32_t i = start + k;
      if(i >= n - 1) i = closed ? i - (n - 1) : (n - 2) - k;
      const Vec3 d = sub(toVec3(smp[i + 1]), toVec3(smp[i]));
      const float l2 = lenSq(d);
      if(l2 > 1e-8f) return mul(d, 1.0f / std::sqrt(l2));
    }
    return {0.0f, 0.0f, 1.0f};
  }

  // Closest point on piece a->b to p with t limited to [tLo, tHi]. planarXZ ignores Y.
  inline float closestOnSegment(const Sample &a, const Sample &b, Vec3 p, bool planarXZ,
                                float tLo, float tHi, float &outDistSq)
  {
    const float dx = b.x - a.x;
    const float dy = planarXZ ? 0.0f : b.y - a.y;
    const float dz = b.z - a.z;
    const float px = p.x - a.x;
    const float py = planarXZ ? 0.0f : p.y - a.y;
    const float pz = p.z - a.z;
    const float l2 = dx * dx + dy * dy + dz * dz;
    float t = l2 > 1e-12f ? (px * dx + py * dy + pz * dz) / l2 : 0.0f;
    t = t < tLo ? tLo : (t > tHi ? tHi : t);
    const float ex = px - dx * t;
    const float ey = py - dy * t;
    const float ez = pz - dz * t;
    outDistSq = ex * ex + ey * ey + ez * ez;
    return t;
  }

  // Arc length of the closest point on the whole curve (brute force over all pieces).
  inline float closestS(const Sample* smp, uint32_t n, Vec3 p, bool planarXZ, float* outDistSq)
  {
    float best = 3.0e38f;
    float bestS = 0.0f;
    if(n == 1) {
      const Sample only = smp[0];
      closestOnSegment(only, only, p, planarXZ, 0.0f, 0.0f, best);
    }
    for(uint32_t i = 0; i + 1 < n; ++i) {
      float d2 = 0.0f;
      const float t = closestOnSegment(smp[i], smp[i + 1], p, planarXZ, 0.0f, 1.0f, d2);
      if(d2 < best) {
        best = d2;
        bestS = smp[i].s + (smp[i + 1].s - smp[i].s) * t;
      }
    }
    if(outDistSq) *outDistSq = (n == 0) ? 0.0f : best;
    return bestS;
  }

  // Closest point restricted to the arc-length window [sMin, sMax] given in UNWRAPPED arc length
  // (callers pass sMin <= sMax; reversed input is swapped). Open curves clamp the window to [0,len].
  // Closed curves: a window reaching outside [0,len] continues across the seam and the result is
  // wrapped into [0,len); a window inside [0,len] never wraps (bounded travel on a loop).
  inline float closestSInRange(const Sample* smp, uint32_t n, bool closed, Vec3 p,
                               float sMin, float sMax, bool planarXZ, float* outDistSq)
  {
    if(n < 2) return closestS(smp, n, p, planarXZ, outDistSq);
    const float len = smp[n - 1].s;
    if(sMax < sMin) { const float tmp = sMin; sMin = sMax; sMax = tmp; }
    const bool crosses = closed && (sMin < 0.0f || sMax > len);
    if(len <= 0.0f || (crosses && sMax - sMin >= len)) return closestS(smp, n, p, planarXZ, outDistSq);

    float start = sMin;
    float end = sMax;
    if(crosses) {
      start = wrapS(sMin, len, true);
      end = start + (sMax - sMin);
    } else {
      start = start < 0.0f ? 0.0f : (start > len ? len : start);
      end = end < 0.0f ? 0.0f : (end > len ? len : end);
    }

    float best = 3.0e38f;
    float bestS = start;
    uint32_t i = segmentAt(smp, n, start);
    float base = 0.0f; // arc length added per completed lap (unwrapped coordinates)
    for(uint32_t visited = 0; visited < n; ++visited) {
      const float aS = smp[i].s + base;
      if(aS > end) break;
      const float segLen = smp[i + 1].s - smp[i].s;
      float tLo = 0.0f;
      float tHi = 1.0f;
      if(segLen > 1e-6f) {
        tLo = (start - aS) / segLen; tLo = tLo < 0.0f ? 0.0f : tLo;
        tHi = (end - aS) / segLen;   tHi = tHi > 1.0f ? 1.0f : tHi;
      }
      if(tLo <= tHi) {
        float d2 = 0.0f;
        const float t = closestOnSegment(smp[i], smp[i + 1], p, planarXZ, tLo, tHi, d2);
        if(d2 < best) { best = d2; bestS = aS + segLen * t; }
      }
      if(++i >= n - 1) {
        if(!crosses) break;
        i = 0;
        base += len;
      }
    }
    if(outDistSq) *outDistSq = best;
    return crosses ? wrapS(bestS, len, true) : bestS;
  }
}
