/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: conditions, see conditionCore.h.
#include "conditionCore.h"

using namespace P64::Conditions::Core;

bool Providers::add(uint32_t hash, Events::Scope scope, uint16_t instance, uint16_t object, uint8_t comp)
{
  for(uint32_t i = 0; i < used; ++i) {
    const auto &e = entries[i];
    if(e.hash == hash && e.instance == instance && e.object == object && e.comp == comp
      && e.scope == static_cast<uint8_t>(scope)) return true;
  }
  if(used >= MAX) return false;
  entries[used++] = {hash, instance, object, comp, static_cast<uint8_t>(scope)};
  if(used > usedPeak) usedPeak = used;
  return true;
}

uint32_t Providers::remove(uint16_t object, uint8_t comp)
{
  // keeps the order of the rest: find() promises the order they were added in
  uint32_t kept = 0;
  for(uint32_t i = 0; i < used; ++i) {
    if(entries[i].object == object && entries[i].comp == comp) continue;
    if(kept != i) entries[kept] = entries[i];
    ++kept;
  }
  const uint32_t removed = used - kept;
  used = kept;
  return removed;
}

uint32_t Providers::find(uint32_t hash, Events::Scope scope, uint16_t instance, Provider* out, uint32_t max) const
{
  uint32_t found = 0;
  for(uint32_t i = 0; i < used && found < max; ++i) {
    const auto &e = entries[i];
    if(e.hash != hash || e.instance != instance || e.scope != static_cast<uint8_t>(scope)) continue;
    out[found++] = e;
  }
  return found;
}
