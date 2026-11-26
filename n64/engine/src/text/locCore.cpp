/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization. The parts of the runtime that do not need libdragon, so the host unit
// tests can run them (tests/locale.cpp).
#include "text/locTable.h"

namespace
{
  uint32_t readU32(const uint8_t* p)
  {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
  }

  uint16_t readU16(const uint8_t* p)
  {
    return uint16_t((uint32_t(p[0]) << 8) | uint32_t(p[1]));
  }
}

bool P64::Loc::TableView::init(const void* fileData, uint32_t fileSize)
{
  *this = {};
  const auto* bytes = static_cast<const uint8_t*>(fileData);
  if(!bytes || fileSize < TABLE_HEADER_SIZE) return false;
  if(bytes[0] != 'P' || bytes[1] != '6' || bytes[2] != '4' || bytes[3] != 'L') return false;
  if(readU16(bytes + 4) != TABLE_VERSION) return false;

  const uint32_t entryCount = readU32(bytes + 8);
  const uint32_t stringStart = readU32(bytes + 12);
  const uint64_t entriesEnd = uint64_t(TABLE_HEADER_SIZE) + uint64_t(entryCount) * TABLE_ENTRY_SIZE;
  if(entriesEnd > fileSize || stringStart < entriesEnd || stringStart > fileSize) return false;

  data = bytes;
  size = fileSize;
  count = entryCount;
  strings = stringStart;
  fileFlags = readU16(bytes + 6);
  return true;
}

int32_t P64::Loc::TableView::find(uint32_t id) const
{
  if(!data) return -1;
  int32_t lo = 0;
  int32_t hi = static_cast<int32_t>(count) - 1;
  while(lo <= hi) {
    const int32_t mid = lo + (hi - lo) / 2;
    const uint32_t midId = idAt(mid);
    if(midId == id) return mid;
    if(midId < id) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}

uint32_t P64::Loc::TableView::idAt(int32_t entry) const
{
  return readU32(data + TABLE_HEADER_SIZE + uint32_t(entry) * TABLE_ENTRY_SIZE);
}

uint8_t P64::Loc::TableView::flagsAt(int32_t entry) const
{
  return data[TABLE_HEADER_SIZE + uint32_t(entry) * TABLE_ENTRY_SIZE + 7];
}

const char* P64::Loc::TableView::textAt(int32_t entry) const
{
  const uint32_t packed = readU32(data + TABLE_HEADER_SIZE + uint32_t(entry) * TABLE_ENTRY_SIZE + 4);
  const uint32_t offset = strings + (packed >> 8);
  if(offset >= size) return "";
  return reinterpret_cast<const char*>(data + offset);
}
