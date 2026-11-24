// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/gameres.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#include "core/inflate.h"

namespace pstf {
namespace {

uint16_t U16(const std::vector<uint8_t>& d, size_t o) { return o + 2 <= d.size() ? uint16_t(d[o] | d[o + 1] << 8) : 0; }
uint32_t U32(const std::vector<uint8_t>& d, size_t o) {
  return o + 4 <= d.size() ? uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24 : 0;
}
std::string Str(const std::vector<uint8_t>& d, size_t o, size_t n) {
  std::string s;
  for (size_t i = 0; i < n && o + i < d.size() && d[o + i]; ++i) s += static_cast<char>(std::toupper(d[o + i]));
  return s;
}
std::string Upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
bool Sig(const std::vector<uint8_t>& d, const char* s) { return d.size() >= 8 && std::memcmp(d.data(), s, 8) == 0; }

std::vector<uint8_t> ReadFile(const std::wstring& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

const char* Ext(uint16_t type) {
  switch (type) {
    case kResBam: return "BAM";
    case kResItm: return "ITM";
    case kResSpl: return "SPL";
    default: return "";
  }
}

}  // namespace

bool ParseKey(const std::vector<uint8_t>& d, KeyIndex* out) {
  if (!Sig(d, "KEY V1  ") || d.size() < 24) return false;
  const uint32_t nbif = U32(d, 8), nres = U32(d, 12), obif = U32(d, 16), ores = U32(d, 20);
  std::vector<std::string> bifs;
  for (uint32_t i = 0; i < nbif; ++i) {
    const size_t e = obif + size_t(i) * 12;
    if (e + 12 > d.size()) return false;
    const uint32_t on = U32(d, e + 4);
    const uint16_t nl = U16(d, e + 8);
    std::string name;
    for (size_t k = 0; k < nl && on + k < d.size() && d[on + k]; ++k) name += d[on + k] == '\\' ? '/' : char(d[on + k]);
    bifs.push_back(name);
  }
  for (uint32_t i = 0; i < nres; ++i) {
    const size_t e = ores + size_t(i) * 14;
    if (e + 14 > d.size()) return false;
    const uint32_t loc = U32(d, e + 10);
    const uint32_t b = loc >> 20;
    if (b >= bifs.size()) continue;
    (*out)[{Str(d, e, 8), U16(d, e + 8)}] = {bifs[b], loc & 0x3FFF};
  }
  return true;
}

std::vector<uint8_t> BifFile(const std::vector<uint8_t>& b, uint32_t index) {
  if (!Sig(b, "BIFFV1  ") || b.size() < 20) return {};
  const uint32_t n = U32(b, 8), oe = U32(b, 16);
  for (uint32_t i = 0; i < n; ++i) {
    const size_t e = oe + size_t(i) * 16;
    if (e + 16 > b.size()) break;
    if ((U32(b, e) & 0x3FFF) != index) continue;
    const uint32_t off = U32(b, e + 4), size = U32(b, e + 8);
    if (size_t(off) + size > b.size()) return {};
    return std::vector<uint8_t>(b.begin() + off, b.begin() + off + size);
  }
  return {};
}

bool BamFirstFrameSize(const std::vector<uint8_t>& in, int* w, int* h) {
  const std::vector<uint8_t>* d = &in;
  std::vector<uint8_t> plain;
  if (Sig(in, "BAMCV1  ")) {
    if (in.size() < 12 || !ZlibInflate(in.data() + 12, in.size() - 12, &plain, U32(in, 8))) return false;
    d = &plain;
  }
  if (Sig(*d, "BAM V2  ") && d->size() >= 32) {
    // V2 (EE, PVRZ blocks): frames u32 at 8, cycles u32 at 12, frame entries at 0x14 (12 bytes: w, h, ...),
    // cycle entries at 0x18 (count u16, first frame index u16)
    const uint32_t frames = U32(*d, 8), cycles = U32(*d, 12), of = U32(*d, 0x14), oc = U32(*d, 0x18);
    if (!frames) return false;
    uint32_t idx = 0;
    if (cycles && U16(*d, oc)) idx = U16(*d, oc + 2);
    if (idx >= frames) idx = 0;
    const size_t e = of + size_t(idx) * 12;
    if (e + 4 > d->size()) return false;
    *w = U16(*d, e);
    *h = U16(*d, e + 2);
    return *w > 0 && *h > 0;
  }
  if (!Sig(*d, "BAM V1  ") || d->size() < 24) return false;
  const uint16_t frames = U16(*d, 8);
  const uint8_t cycles = (*d)[10];
  const uint32_t of = U32(*d, 12);
  if (!frames) return false;
  // frame 0 of cycle 0 through the lookup table when there is one, else entry 0
  uint32_t idx = 0;
  if (cycles) {
    const size_t cyc = of + size_t(frames) * 12;
    const uint16_t count = U16(*d, cyc), first = U16(*d, cyc + 2);
    const uint32_t ol = U32(*d, 20);
    if (count && size_t(ol) + (size_t(first) + 1) * 2 <= d->size()) idx = U16(*d, ol + size_t(first) * 2);
    if (idx >= frames) idx = 0;
  }
  const size_t e = of + size_t(idx) * 12;
  if (e + 4 > d->size()) return false;
  *w = U16(*d, e);
  *h = U16(*d, e + 2);
  return *w > 0 && *h > 0;
}

bool ParseSpell(const std::vector<uint8_t>& d, SpellFacts* out) {
  if (!Sig(d, "SPL V1  ") || d.size() < 0x72) return false;
  out->name = static_cast<int32_t>(U32(d, 0x08));
  out->flags = U32(d, 0x18);
  out->icon = Str(d, 0x3A, 8);
  if (out->icon.empty() && U16(d, 0x68) > 0) out->icon = Str(d, U32(d, 0x64) + 4, 8);
  return true;
}

bool ParseItem(const std::vector<uint8_t>& d, ItemFacts* out) {
  if (!Sig(d, "ITM V1  ") || d.size() < 0x72) return false;
  const int32_t idn = static_cast<int32_t>(U32(d, 0x0C)), un = static_cast<int32_t>(U32(d, 0x08));
  out->name = idn >= 0 ? idn : un;
  out->icon = Str(d, 0x3A, 8);
  return true;
}

std::vector<uint8_t> GameRes::Read(const std::string& raw, uint16_t type) {
  const std::string name = Upper(raw);
  if (name.empty() || !*Ext(type)) return {};
  const std::wstring file(name.begin(), name.end());
  const char* ext = Ext(type);
  std::vector<uint8_t> d = ReadFile(dir_ + L"\\override\\" + file + L"." + std::wstring(ext, ext + 3));
  if (!d.empty()) return d;
  if (!key_loaded_) {
    key_loaded_ = true;
    ParseKey(ReadFile(dir_ + L"\\chitin.key"), &key_);
  }
  const auto it = key_.find({name, type});
  if (it == key_.end()) return {};
  auto b = bifs_.find(it->second.bif);
  if (b == bifs_.end()) {
    std::wstring rel(it->second.bif.begin(), it->second.bif.end());
    std::replace(rel.begin(), rel.end(), L'/', L'\\');
    b = bifs_.emplace(it->second.bif, ReadFile(dir_ + L"\\" + rel)).first;
  }
  return BifFile(b->second, it->second.index);
}

std::pair<int, int> GameRes::BamSize(const std::string& raw) {
  const std::string name = Upper(raw);
  const auto it = bam_sizes_.find(name);
  if (it != bam_sizes_.end()) return it->second;
  int w = 0, h = 0;
  if (!BamFirstFrameSize(Read(name, kResBam), &w, &h)) w = h = 0;
  return bam_sizes_[name] = {w, h};
}

const SpellFacts* GameRes::Spell(const std::string& raw) {
  const std::string name = Upper(raw);
  const auto it = spells_.find(name);
  if (it != spells_.end()) return &it->second;
  if (missing_.count("S" + name)) return nullptr;
  SpellFacts f;
  if (!ParseSpell(Read(name, kResSpl), &f)) {
    missing_["S" + name] = true;
    return nullptr;
  }
  return &(spells_[name] = f);
}

const ItemFacts* GameRes::Item(const std::string& raw) {
  const std::string name = Upper(raw);
  const auto it = items_.find(name);
  if (it != items_.end()) return &it->second;
  if (missing_.count("I" + name)) return nullptr;
  ItemFacts f;
  if (!ParseItem(Read(name, kResItm), &f)) {
    missing_["I" + name] = true;
    return nullptr;
  }
  return &(items_[name] = f);
}

}  // namespace pstf
