// SPDX-License-Identifier: GPL-2.0-or-later
// 8-member party: the CInfGame party arrays' new layout and the checks of the generated site table
// (src/engine/party_sites.h). Pure: shared by the DLL (in memory), pstf_verify (on-disk exe) and the
// unit tests.
//
// Engine layout (lin CInfGame::AddCharacterToParty 375A70 / win 1402D0A70): +0xA1E8 i32[6] fixed-order
// (portrait) slots, +0xA200 i32[6] party order, +0xA218 i16 count. CInfGame is allocated with
// operator new(0xD140) (CBaldurChitin::Init win 140141255); we allocate 0xD180 and keep both arrays,
// 8 entries each, at +0xD140 and +0xD160. The count stays at +0xA218.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/party_sites.h"

namespace pstf::party {

using namespace pstf::sym::party;

// New offset of a field that addressed the old arrays (displacement or immediate), 0 if not one.
constexpr uint32_t MapOffset(uint32_t old_off) {
  if (old_off >= kOldFixed && old_off < kOldOrder) return kNewFixed + (old_off - kOldFixed);
  if (old_off >= kOldOrder && old_off < kCount) return kNewOrder + (old_off - kOldOrder);
  return 0;
}

// The bytes a site has after retargeting.
inline void SiteNewBytes(const Site& s, uint8_t out[15]) {
  std::memcpy(out, s.bytes, s.len);
  std::memcpy(out + s.field, &s.new_val, 4);
}

// Consistency of the generated tables: every site's field holds its old value, the new value is
// MapOffset(old) (sites a patch covers are dropped by the generator), sites and
// patches never overlap, patch lengths fit. Returns nullptr when fine, else what is wrong.
inline const char* CheckTables(uint32_t* bad_rva = nullptr) {
  auto fail = [&](uint32_t rva, const char* why) {
    if (bad_rva) *bad_rva = rva;
    return why;
  };
  for (const Site& s : kSites) {
    if (s.len == 0 || s.len > 15 || s.field + 4u > s.len) return fail(s.rva, "site field outside the instruction");
    uint32_t v;
    std::memcpy(&v, s.bytes + s.field, 4);
    if (v != s.old_val) return fail(s.rva, "site field != old value");
    if (MapOffset(s.old_val) != s.new_val) return fail(s.rva, "site new value != MapOffset(old)");
  }
  for (const Patch& p : kPatches) {
    if (!p.why) continue;  // empty-table placeholder
    if (p.len == 0 || p.len > sizeof p.old_bytes) return fail(p.rva, "patch length");
    if (std::memcmp(p.old_bytes, p.new_bytes, p.len) == 0) return fail(p.rva, "patch changes nothing");
    for (const Site& s : kSites)
      if (p.rva < s.rva + s.len && s.rva < p.rva + p.len) return fail(p.rva, "patch overlaps a site");
  }
  return nullptr;
}

// Applies every site and patch through `check(rva, expected, n)` then `write(rva, bytes, n)`.
// All-or-nothing: the first pass only checks. Returns the number of writes, or -1 (bad_rva set).
template <class Check, class Write>
int ApplyAll(Check check, Write write, uint32_t* bad_rva = nullptr) {
  for (const Site& s : kSites)
    if (!check(s.rva, s.bytes, s.len)) {
      if (bad_rva) *bad_rva = s.rva;
      return -1;
    }
  for (const Patch& p : kPatches)
    if (p.why && !check(p.rva, p.old_bytes, p.len)) {
      if (bad_rva) *bad_rva = p.rva;
      return -1;
    }
  int n = 0;
  for (const Site& s : kSites) {
    uint8_t b[15];
    SiteNewBytes(s, b);
    write(s.rva, b, s.len);
    ++n;
  }
  for (const Patch& p : kPatches)
    if (p.why) {
      write(p.rva, p.new_bytes, p.len);
      ++n;
    }
  return n;
}

}  // namespace pstf::party
