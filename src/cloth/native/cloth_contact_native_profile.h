#pragma once
#include <cstddef>
#include <cstdint>

namespace poser_cloth_contact_native {
// Reviewed 1.6.3 GameAssembly: the four-container job, live list-length alias,
// queue clear and schedule dependency/return ABI are unchanged. The producer
// still leaves indexCount null. No worker or container write is added here.
// These profiles certify only this scheduling adapter, not the solver kernels.
inline constexpr bool ContactAdapter163Reviewed = true;
struct Code { size_t rva, bytes; uint64_t hash; };
struct Profile {
  const char *name;
  Code update, execute[2], queue[2], clear, schedule[2], support[2];
  size_t scheduleCall[2];
};
inline constexpr Profile Legacy{
  "legacy",
  {0,384,0x17cd35be18c07ca5ULL},
  {{0,91,0x5773f42d899326d0ULL},{0,91,0x34c9caf72e330ef2ULL}},
  {{0,190,0x87a5050d0e9e699eULL},{0,190,0xe461448fd60ed6bcULL}},
  {0,31,0x13df3f07871d6310ULL},
  {{0,367,0x482d29a4aa3a07ceULL},{0,367,0xd90fc83366eb9f98ULL}},
  {},{}
};
inline constexpr Profile Native163{
  "1.6.3",
  {0x89ea3c4,384,0x3e392eecf6b27ac0ULL},
  {{0x8a055fc,91,0xae074f21d34e1b50ULL},{0x8a06890,91,0x14b91f94146bf174ULL}},
  {{0x89f9820,190,0x8d6ae6e0149bfe52ULL},{0x89fcc54,190,0xcf1255c447d85efaULL}},
  {0x6e4224,31,0x6773704f15e7e84bULL},
  {{0x89d76a8,367,0x80a7824d145e95b9ULL},{0x89d7818,367,0x4f8651049e84e5b3ULL}},
  {{0xc70a24,73,0xace3b5becd8344f8ULL},{0xc70814,97,0x1a4e94463824046aULL}},
  {0xeb,0x14d}
};
inline bool AddressMatches(uintptr_t module, uintptr_t code, Code check) {
  return module && code && check.bytes && (!check.rva ||
      (check.rva<=UINTPTR_MAX-module && code==module+check.rva));
}
}
