/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, section 4.8). The string table of one language
// as the build writes it (rom:/p64/loc/<code>.loc), and a reader for it.
// This header must stay free of engine and libdragon includes: the editor and the host unit
// tests include it too. All numbers in the file are big-endian.
//
//   offset 0   char[4]  magic "P64L"
//          4   u16      version (1)
//          6   u16      flags (FILE_PSEUDO)
//          8   u32      count
//         12   u32      offset of the strings, from the start of the file
//         16   entries[count], sorted by id: u32 id, u32 (string offset << 8 | entry flags)
//              strings: zero-terminated, at the given offset from the start of the strings
#pragma once
#include <cstddef>
#include <cstdint>

namespace P64::Loc
{
  constexpr uint16_t TABLE_VERSION = 1;
  constexpr uint32_t TABLE_HEADER_SIZE = 16;
  constexpr uint32_t TABLE_ENTRY_SIZE = 8;

  constexpr uint16_t FILE_PSEUDO = 1 << 0;   // a generated test language

  constexpr uint8_t ENTRY_FALLBACK = 1 << 0; // no translation yet: the text is the source language's

  /** Reads a table in memory. It never copies or owns the data. */
  struct TableView
  {
    const uint8_t* data{};
    uint32_t size{};
    uint32_t count{};
    uint32_t strings{};
    uint16_t fileFlags{};

    /** False when the data is not a table of this version or does not fit in 'size'. */
    bool init(const void* fileData, uint32_t fileSize);

    /** The entry of an id, or -1. */
    int32_t find(uint32_t id) const;

    uint32_t idAt(int32_t entry) const;
    uint8_t flagsAt(int32_t entry) const;
    const char* textAt(int32_t entry) const;

    bool valid() const { return data != nullptr; }
  };
}
