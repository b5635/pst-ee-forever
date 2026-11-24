// SPDX-License-Identifier: GPL-2.0-or-later
// Game resource parsers: KEY V1, BIFF V1, BAM/BAMC frame size, SPL/ITM facts (synthetic files laid out
// as IESDP key_v1, bif_v1, bam_v1, spl_v1, itm_v1 describe them).
#include <cstring>
#include <string>
#include <vector>

#include "core/gameres.h"
#include "mini_test.h"

using namespace pstf;

namespace {
void Put16(std::vector<uint8_t>* d, size_t o, uint16_t v) {
  if (d->size() < o + 2) d->resize(o + 2);
  (*d)[o] = uint8_t(v), (*d)[o + 1] = uint8_t(v >> 8);
}
void Put32(std::vector<uint8_t>* d, size_t o, uint32_t v) {
  if (d->size() < o + 4) d->resize(o + 4);
  for (int i = 0; i < 4; ++i) (*d)[o + i] = uint8_t(v >> (8 * i));
}
void PutStr(std::vector<uint8_t>* d, size_t o, const char* s, size_t n) {
  if (d->size() < o + n) d->resize(o + n);
  for (size_t i = 0; i < n; ++i) (*d)[o + i] = i < std::strlen(s) ? uint8_t(s[i]) : 0;
}
// BAM V1: 2 frames (30x23, 61x47), one cycle whose first lookup entry is frame `first`.
std::vector<uint8_t> Bam(uint16_t first) {
  std::vector<uint8_t> d;
  PutStr(&d, 0, "BAM V1  ", 8);
  Put16(&d, 8, 2);
  d.resize(24);
  d[10] = 1;
  Put32(&d, 12, 24);           // frame entries
  Put32(&d, 20, 24 + 24 + 4);  // lookup table after the cycle entry
  Put16(&d, 24, 30), Put16(&d, 26, 23);
  Put16(&d, 36, 61), Put16(&d, 38, 47);
  Put16(&d, 48, 1), Put16(&d, 50, 0);  // cycle 0: 1 frame from lookup index 0
  Put16(&d, 52, first);
  return d;
}
uint32_t Adler(const std::vector<uint8_t>& d) {
  uint32_t a = 1, b = 0;
  for (uint8_t c : d) a = (a + c) % 65521, b = (b + a) % 65521;
  return b << 16 | a;
}
}  // namespace

TEST_CASE("gameres: BAM first frame size, through the cycle lookup, and BAMC") {
  int w = 0, h = 0;
  CHECK(BamFirstFrameSize(Bam(0), &w, &h) && w == 30 && h == 23);
  CHECK(BamFirstFrameSize(Bam(1), &w, &h) && w == 61 && h == 47);
  // BAMC: zlib stream with one stored block
  const std::vector<uint8_t> plain = Bam(1);
  std::vector<uint8_t> c;
  PutStr(&c, 0, "BAMCV1  ", 8);
  Put32(&c, 8, uint32_t(plain.size()));
  c.push_back(0x78), c.push_back(0x01), c.push_back(0x01);
  const uint16_t n = uint16_t(plain.size());
  c.push_back(uint8_t(n)), c.push_back(uint8_t(n >> 8)), c.push_back(uint8_t(~n)), c.push_back(uint8_t(~n >> 8));
  c.insert(c.end(), plain.begin(), plain.end());
  const uint32_t ad = Adler(plain);
  for (int i = 3; i >= 0; --i) c.push_back(uint8_t(ad >> (8 * i)));
  w = h = 0;
  CHECK(BamFirstFrameSize(c, &w, &h) && w == 61 && h == 47);
  CHECK(!BamFirstFrameSize(std::vector<uint8_t>(10, 0), &w, &h));
  // BAM V2 (EE spell icons such as SP066: 2 frames, cycles at 0x20, frames at 0x28, frame 0 256 x 256)
  std::vector<uint8_t> v2;
  PutStr(&v2, 0, "BAM V2  ", 8);
  Put32(&v2, 8, 2), Put32(&v2, 12, 2), Put32(&v2, 16, 10), Put32(&v2, 0x14, 0x28), Put32(&v2, 0x18, 0x20);
  Put16(&v2, 0x20, 1), Put16(&v2, 0x22, 1);  // cycle 0 starts at frame 1
  Put16(&v2, 0x28, 256), Put16(&v2, 0x2A, 256);
  Put16(&v2, 0x34, 64), Put16(&v2, 0x36, 48);
  CHECK(BamFirstFrameSize(v2, &w, &h) && w == 64 && h == 48);
}

TEST_CASE("gameres: KEY V1 and BIFF V1 locate a file") {
  std::vector<uint8_t> key;
  PutStr(&key, 0, "KEY V1  ", 8);
  Put32(&key, 8, 1), Put32(&key, 12, 1), Put32(&key, 16, 24), Put32(&key, 20, 48);
  Put32(&key, 24, 0), Put32(&key, 28, 36), Put16(&key, 32, 12), Put16(&key, 34, 1);
  PutStr(&key, 36, "data\\a.bif", 12);
  PutStr(&key, 48, "spwi101", 8);
  Put16(&key, 56, kResSpl);
  Put32(&key, 58, (0u << 20) | 3);
  KeyIndex idx;
  CHECK(ParseKey(key, &idx));
  const auto it = idx.find({"SPWI101", kResSpl});
  CHECK(it != idx.end() && it->second.bif == "data/a.bif" && it->second.index == 3);
  std::vector<uint8_t> bif;
  PutStr(&bif, 0, "BIFFV1  ", 8);
  Put32(&bif, 8, 2), Put32(&bif, 12, 0), Put32(&bif, 16, 20);
  Put32(&bif, 20, 2), Put32(&bif, 24, 52), Put32(&bif, 28, 2);
  Put32(&bif, 36, 3), Put32(&bif, 40, 54), Put32(&bif, 44, 3);
  PutStr(&bif, 52, "xxabc", 5);
  const auto f = BifFile(bif, 3);
  CHECK(f.size() == 3 && f[0] == 'a' && f[2] == 'c');
  CHECK(BifFile(bif, 9).empty());
}

TEST_CASE("gameres: spell and item facts") {
  std::vector<uint8_t> spl;
  PutStr(&spl, 0, "SPL V1  ", 8);
  Put32(&spl, 0x08, 12345);
  Put32(&spl, 0x18, 0x400);
  PutStr(&spl, 0x3A, "spwi101c", 8);
  spl.resize(0x72);
  SpellFacts s;
  CHECK(ParseSpell(spl, &s) && s.name == 12345 && s.flags == 0x400 && s.icon == "SPWI101C");
  // no spellbook icon: the first ability's
  PutStr(&spl, 0x3A, "", 8);
  Put32(&spl, 0x64, 0x72), Put16(&spl, 0x68, 1);
  PutStr(&spl, 0x72 + 4, "spwi101b", 8);
  CHECK(ParseSpell(spl, &s) && s.icon == "SPWI101B");
  std::vector<uint8_t> itm;
  PutStr(&itm, 0, "ITM V1  ", 8);
  Put32(&itm, 0x08, 7), Put32(&itm, 0x0C, 0xFFFFFFFF);
  PutStr(&itm, 0x3A, "ii020", 8);
  itm.resize(0x72);
  ItemFacts i;
  CHECK(ParseItem(itm, &i) && i.name == 7 && i.icon == "II020");
}
