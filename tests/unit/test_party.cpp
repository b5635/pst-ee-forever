// SPDX-License-Identifier: GPL-2.0-or-later
// 8-member party: the moved-array mapping and the generated site/patch tables (engine/party_layout.h).
#include <cstring>
#include <map>
#include <vector>

#include "engine/party_layout.h"
#include "engine/symbols.h"
#include "mini_test.h"

using namespace pstf;
using namespace pstf::sym::party;

TEST_CASE("party: offsets map into the new 8-slot arrays") {
  CHECK(party::MapOffset(0xA1E8) == 0xD140);  // fixed[0]
  CHECK(party::MapOffset(0xA1FC) == 0xD154);  // fixed[5]
  CHECK(party::MapOffset(0xA200) == 0xD160);  // order[0] == end of the new fixed array
  CHECK(party::MapOffset(0xA214) == 0xD174);  // order[5]
  CHECK(party::MapOffset(0xA218) == 0);       // the count stays
  CHECK(party::MapOffset(0xA1E4) == 0);
  CHECK(kNewOrder - kNewFixed == 4 * kMax);
  CHECK(kNewOrder + 4 * kMax == kNewSize);
  CHECK(kNewFixed == kOldSize);  // the arrays live past the old end of CInfGame
}

TEST_CASE("party: generated tables are consistent") {
  uint32_t bad = 0;
  const char* why = party::CheckTables(&bad);
  CHECK(why == nullptr);
  CHECK(std::size(kSites) > 400);  // tools/party_refs.py: 480 instructions, minus the superseded ones
}

TEST_CASE("party: key patches are in the table") {
  std::map<uint32_t, const Patch*> by;
  for (const Patch& p : kPatches) by[p.rva] = &p;
  // operator new(sizeof CInfGame) 0xD140 -> 0xD180 (win 140141255).
  const Patch* alloc = by[0x141255];
  CHECK(alloc && alloc->len == 5 && alloc->new_bytes[1] == 0x80 && alloc->new_bytes[2] == 0xD1);
  // AddCharacterToParty: count >= 6 -> >= 8 (win 1402D0AC2 cmp word [rbx+A218h], 6).
  const Patch* full = by[0x2D0AC2];
  CHECK(full && full->old_bytes[full->len - 1] == 6 && full->new_bytes[full->len - 1] == 8);
  // MAX_CHARACTERS (rdata 14062E211) 6 -> 8.
  const Patch* maxc = by[0x62E211];
  CHECK(maxc && maxc->len == 1 && maxc->old_bytes[0] == 6 && maxc->new_bytes[0] == 8);
  // RemoveCharacterFromParty: order[count-1] uses the order array (D15C), not fixed[5] (D154).
  const Patch* rem = by[0x2EB7DB];
  uint32_t disp = 0;
  CHECK(rem && rem->len == 8);
  if (rem) std::memcpy(&disp, rem->new_bytes + 4, 4);
  CHECK(disp == kNewOrder - 4);
  // The superseded instruction is not also retargeted.
  bool site = false;
  for (const Site& s : kSites) site |= s.rva == 0x2EB7DB;
  CHECK(!site);
}

TEST_CASE("party: ApplyAll is all-or-nothing") {
  // Memory with every site and patch at its on-disk bytes, except one: nothing is written.
  std::map<uint32_t, std::vector<uint8_t>> mem;
  for (const Site& s : kSites) mem[s.rva].assign(s.bytes, s.bytes + s.len);
  for (const Patch& p : kPatches)
    if (p.why) mem[p.rva].assign(p.old_bytes, p.old_bytes + p.len);
  mem[kSites[3].rva][0] ^= 0xFF;
  int writes = 0;
  auto check = [&](uint32_t rva, const uint8_t* b, size_t n) {
    auto& m = mem[rva];
    return m.size() == n && std::memcmp(m.data(), b, n) == 0;
  };
  auto write = [&](uint32_t, const uint8_t*, size_t) { ++writes; };
  uint32_t bad = 0;
  CHECK(party::ApplyAll(check, write, &bad) == -1);
  CHECK(writes == 0);
  CHECK(bad == kSites[3].rva);
  mem[kSites[3].rva][0] ^= 0xFF;
  const int n = party::ApplyAll(check, write, &bad);
  CHECK(n == writes);
  CHECK(n > 400);
}

TEST_CASE("party: formation point 7 extends the engine's 7 points") {
  // 7 rotated points as CAIGroup::GetOffsets leaves them (x1000 grid units), room for 8.
  int32_t pts[16] = {0, 0, 1000, 2000, -1000, 2000, 2000, 4000, -2000, 4000, 3000, 6000, -3000, 6000, 77, 77};
  party::ExtendFormation(pts, 7);
  CHECK(pts[14] == -3000 + (-3000 + 2000));  // 6 + (6 - 4), same side
  CHECK(pts[15] == 6000 + (6000 - 4000));
  CHECK(pts[12] == -3000);  // the engine's points stay
  int32_t one[16] = {5, 7};
  party::ExtendFormation(one, 1);
  CHECK(one[2] == 5 && one[3] == 7);  // a lone point: the next ones stack on it (open square search)
  CHECK(one[14] == 5 && one[15] == 7);
  // the size patch: operator new(8 * n) -> 16 * n in GetOffsets (win 14012577F)
  bool found = false;
  for (const Patch& p : kPatches)
    if (p.rva == 0x12577F) found = p.len == 5 && p.old_bytes[1] == 0x08 && p.new_bytes[1] == 0x10;
  CHECK(found);
}

TEST_CASE("party: manifest bytes after the layout (other modules' checks)") {
  // tbc selection lock: OnPortraitLClick.select (win 1402EA411) holds mov edx,[rbx+rbp*4+0A200h],
  // a retargeted site: after the layout it reads +0xD160, the rest of the symbol is unchanged.
  uint8_t b[sizeof pstf::sym::kPortraitSelectBytes];
  std::memcpy(b, pstf::sym::kPortraitSelectBytes, sizeof b);
  party::RewriteBytes(pstf::sym::kRvaPortraitSelect, b, sizeof b);
  const uint8_t want[] = {0xE8, 0xAA, 0x19, 0x01, 0x00, 0x8B, 0x94, 0xAB, 0x60, 0xD1, 0x00, 0x00,
                          0x41, 0xB0, 0x01, 0x48, 0x8B, 0xCB, 0xE8, 0x08, 0x7E, 0x00, 0x00};
  CHECK(std::memcmp(b, want, sizeof b) == 0);
  // a window starting inside the site: only the overlapping bytes change
  uint8_t tail[3] = {0xA2, 0x00, 0x00};
  party::RewriteBytes(pstf::sym::kRvaPortraitSelect + 9, tail, 3);
  CHECK(tail[0] == 0xD1 && tail[1] == 0x00 && tail[2] == 0x00);
  // a patch: GetOffsets' allocation size
  uint8_t mov[5] = {0xB8, 0x08, 0x00, 0x00, 0x00};
  party::RewriteBytes(0x12577F, mov, 5);
  CHECK(mov[1] == 0x10);
}
