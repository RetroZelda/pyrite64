/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "scene/object.h"
#include "scene/components/spline.h"

#include <new>
#include <string.h>

namespace SM = P64::SplineMath;

namespace
{
  P64::Comp::Spline* liveHead = nullptr;
  uint32_t liveEpoch = 0;

  inline fm_vec3_t toFm(const SM::Vec3 &v) { return fm_vec3_t{v.x, v.y, v.z}; }
  inline SM::Vec3 toSm(const fm_vec3_t &v) { return SM::Vec3{v.x, v.y, v.z}; }

  // Pure parse of the payload header (getAllocSize is called twice by the loader).
  struct Layout
  {
    bool valid{false};
    bool closedEff{false};
    uint8_t flags{0};
    uint8_t type{0};
    uint8_t steps{1};
    uint8_t count{0};
    uint16_t id{0};
    uint32_t samples{0};
  };

  Layout parseLayout(const uint8_t* in)
  {
    Layout l{};
    if(in[SM::OFF_VERSION] != SM::FORMAT_VERSION) return l;
    l.count = (uint8_t)SM::clampCount(in[SM::OFF_COUNT]);
    l.type = in[SM::OFF_TYPE] == SM::TYPE_CATMULL_ROM ? SM::TYPE_CATMULL_ROM : SM::TYPE_LINEAR;
    l.flags = in[SM::OFF_FLAGS] & SM::FLAG_MASK;
    l.closedEff = SM::isClosed(l.count, (l.flags & SM::FLAG_CLOSED) != 0);
    l.steps = (uint8_t)SM::effectiveSubdiv(l.type, in[SM::OFF_SUBDIV]);
    l.id = (uint16_t)((in[SM::OFF_PATH_ID] << 8) | in[SM::OFF_PATH_ID + 1]);
    l.samples = SM::sampleCount(l.count, l.closedEff, l.type, l.steps);
    l.valid = true;
    return l;
  }
}

uint32_t P64::Comp::Spline::getAllocSize(void* initData)
{
  const Layout l = parseLayout((const uint8_t*)initData);
  if(!l.valid) return sizeof(Spline);
  return sizeof(Spline) + l.samples * sizeof(SM::Sample) + ((l.count + 3u) & ~3u);
}

void P64::Comp::Spline::initDelete(Object &obj, Spline* data, void* initData)
{
  if(initData == nullptr) { // destroy (~Object already ran onDisable if the object was enabled)
    unlink(data);  // no-op then, kept for callers that destroy without onDisable
    ++liveEpoch;   // always: a disabled object's spline may still be cached through fromObject()
    data->~Spline();
    return;
  }

  // not linked here: the live list only holds enabled splines (onEnable / onDisable)
  new(data) Spline();
  data->owner = &obj;

  // initData is only 4-byte aligned and is freed after init: copy, keep no pointers into it
  const auto *in = (const uint8_t*)initData;
  const Layout l = parseLayout(in);
  if(!l.valid) {
    debugf("[Spline] object %u: unsupported data version %u, path ignored\n",
      (unsigned)obj.id, (unsigned)in[SM::OFF_VERSION]);
    return;
  }

  SM::Vec3 pts[SM::MAX_POINTS];
  memcpy(pts, in + SM::OFF_POINTS, l.count * sizeof(SM::Vec3));
  if((l.flags & SM::FLAG_WORLD) == 0) {
    // prefab build: object-local points, placed with the (spawn) transform set before init
    for(uint32_t i = 0; i < l.count; ++i) {
      pts[i] = toSm(obj.outOfLocalSpace(toFm(pts[i])));
    }
  }

  data->pathId = l.id;
  data->pathFlags = l.closedEff ? SM::FLAG_CLOSED : 0;
  data->curveType = l.type;
  data->subdiv = l.steps;
  data->pointCount = l.count;

  SM::Sample* smp = data->samplesMut();
  const uint32_t written = SM::tessellate(pts, l.count, l.closedEff, l.type, l.steps, smp);
  data->sampleCount = (uint16_t)written;
  data->length = written ? smp[written - 1].s : 0.0f;

  // the waits follow ALL points written (a count above MAX_POINTS is clamped, not re-laid out),
  // and are stored behind the samples, so sampleCount must be set first
  memcpy(data->waitsMut(), in + SM::OFF_POINTS + in[SM::OFF_COUNT] * sizeof(SM::Vec3), l.count);
}

void P64::Comp::Spline::onEnable(Object&, Spline* data)
{
  link(data);
}

void P64::Comp::Spline::onDisable(Object&, Spline* data)
{
  if(unlink(data)) ++liveEpoch;
}

void P64::Comp::Spline::link(Spline* s)
{
  // the scene also sends onEnable when only the visibility of an enabled object changes:
  // linking twice would close the list into a loop
  for(const Spline* it = liveHead; it; it = it->nextLive) {
    if(it == s) return;
  }
  s->nextLive = liveHead;
  liveHead = s;
}

bool P64::Comp::Spline::unlink(Spline* s)
{
  bool found = false;
  for(Spline** it = &liveHead; *it; it = &(*it)->nextLive) {
    if(*it == s) { *it = s->nextLive; found = true; break; }
  }
  s->nextLive = nullptr;
  return found;
}

P64::Comp::Spline* P64::Comp::Spline::getFirst() { return liveHead; }
uint32_t P64::Comp::Spline::getEpoch() { return liveEpoch; }

P64::Comp::Spline* P64::Comp::Spline::fromObject(const Object* obj)
{
  return obj ? obj->getComponent<Spline>() : nullptr;
}

P64::Comp::Spline* P64::Comp::Spline::findByPathId(uint16_t wantedId, const fm_vec3_t &nearPos)
{
  if(wantedId == 0) return nullptr;
  Spline* best = nullptr;
  float bestDistSq = 0.0f;
  for(Spline* it = liveHead; it; it = it->nextLive) {
    if(it->pathId != wantedId || !it->isValid()) continue;
    // 3D, not planar: stacked copies of a room on several floors tie (or overlap) in XZ
    float d2 = 0.0f;
    (void)it->getClosest(nearPos, false, &d2);
    if(!best || d2 < bestDistSq) { best = it; bestDistSq = d2; }
  }
  return best;
}

fm_vec3_t P64::Comp::Spline::getPoint(uint32_t idx) const
{
  if(pointCount == 0 || sampleCount == 0) return fm_vec3_t{0.0f, 0.0f, 0.0f};
  if(idx >= pointCount) idx = pointCount - 1;
  uint32_t k = SM::pointSampleIndex(idx, subdiv);
  if(k >= sampleCount) k = sampleCount - 1u;
  return toFm(SM::toVec3(getSamples()[k]));
}

float P64::Comp::Spline::getPointDistance(uint32_t idx) const
{
  if(pointCount == 0 || sampleCount == 0) return 0.0f;
  if(idx >= pointCount) idx = pointCount - 1;
  uint32_t k = SM::pointSampleIndex(idx, subdiv);
  if(k >= sampleCount) k = sampleCount - 1u;
  return getSamples()[k].s;
}

float P64::Comp::Spline::getPointWait(uint32_t idx) const
{
  if(idx >= pointCount) return 0.0f;
  return (float)waits()[idx] * 0.1f;
}

uint32_t P64::Comp::Spline::getSegmentAt(float s) const
{
  const uint32_t segs = getSegmentCount();
  if(segs == 0 || sampleCount < 2) return 0;
  const uint32_t k = SM::segmentAt(getSamples(), sampleCount, wrapDistance(s)) / subdiv;
  return k < segs ? k : segs - 1u;
}

float P64::Comp::Spline::wrapDistance(float s) const
{
  return SM::wrapS(s, length, isClosed());
}

fm_vec3_t P64::Comp::Spline::getPosition(float s) const
{
  return toFm(SM::positionAt(getSamples(), sampleCount, isClosed(), s));
}

fm_vec3_t P64::Comp::Spline::getDirection(float s) const
{
  return toFm(SM::directionAt(getSamples(), sampleCount, isClosed(), s));
}

float P64::Comp::Spline::getClosest(const fm_vec3_t &p, bool planarXZ, float *outDistSq) const
{
  return SM::closestS(getSamples(), sampleCount, toSm(p), planarXZ, outDistSq);
}

float P64::Comp::Spline::getClosestInRange(const fm_vec3_t &p, float sMin, float sMax, bool planarXZ, float *outDistSq) const
{
  return SM::closestSInRange(getSamples(), sampleCount, isClosed(), toSm(p), sMin, sMax, planarXZ, outDistSq);
}
