
#pragma once

#include <stdint.h>

namespace P64::Lib::Hash
{
    // This should provide an constexpr-able 32bit FNV-1a hash 
    // this is to get a unique compile-time number with a stronger ability to combine with other hash value
    // This is O(n) and recursive, so its prolly best to limit the runtime use
    constexpr uint32_t PRIME_32 = 0x1000193u;
    constexpr uint32_t OFFSET_BASIS_32 = 0x811C9DC5u;
    constexpr uint32_t fnv32(const char* toHash, uint32_t hashBasis = OFFSET_BASIS_32)
    {
            constexpr char NullTerm = '\0';
            return (toHash[0] == NullTerm) ? hashBasis : fnv32(toHash + 1, (hashBasis ^ static_cast<uint32_t>(toHash[0])) * PRIME_32);
    }

    // same thing but 16 bit, for when you want a smaller hash and are ok with more collisions
    constexpr uint16_t PRIME_16 = 0x8003u;
    constexpr uint16_t OFFSET_BASIS_16 = 0x9DC5u;
    constexpr uint16_t fnv16(const char* toHash, uint16_t hashBasis = OFFSET_BASIS_16)
    {
            constexpr char NullTerm = '\0';
            return (toHash[0] == NullTerm) ? hashBasis : fnv16(toHash + 1, (hashBasis ^ static_cast<uint16_t>(toHash[0])) * PRIME_16);
    }
}