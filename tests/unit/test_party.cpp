// SPDX-License-Identifier: GPL-2.0-or-later
// 8-member party: the moved-array mapping and the generated site/patch tables (engine/party_layout.h).
#include <cstring>
#include <map>
#include <vector>

#include "engine/party_layout.h"
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
