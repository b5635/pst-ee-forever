// SPDX-License-Identifier: GPL-2.0-or-later
// Game resources from disk, the way the engine finds them: `override\NAME.EXT` first, then
// chitin.key -> BIFF V1 (IESDP key_v1 / bif_v1). Used for UI facts the menu cannot ask the engine:
// a BAM's frame size (icons keep their aspect) and a spell's icon, name and hostile flag (status icons).
// The parsers are pure (unit-tested); GameRes reads files and caches.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace pstf {

struct KeyEntry {
  std::string bif;   // relative path, '/' separators
  uint32_t index = 0;  // file index inside the BIF (locator & 0x3FFF)
};
// KEY V1: (UPPERCASE name, type code) -> where.
using KeyIndex = std::map<std::pair<std::string, uint16_t>, KeyEntry>;
bool ParseKey(const std::vector<uint8_t>& key, KeyIndex* out);
// The bytes of file `index` in a BIFF V1 image; empty when absent.
std::vector<uint8_t> BifFile(const std::vector<uint8_t>& bif, uint32_t index);
// Frame 0 of the first cycle of a BAM V1 (or BAMC, inflated): width/height; false on a bad file.
bool BamFirstFrameSize(const std::vector<uint8_t>& bam, int* w, int* h);

struct SpellFacts {
  int32_t name = -1;    // identified name strref (0x08)
  uint32_t flags = 0;   // 0x18 (bit 10 = hostile)
  std::string icon;     // spellbook icon (0x3A), else the first ability's icon
};
bool ParseSpell(const std::vector<uint8_t>& spl, SpellFacts* out);
struct ItemFacts {
  int32_t name = -1;    // identified name (0x0C), else unidentified (0x08)
  std::string icon;     // inventory icon (0x3A)
};
bool ParseItem(const std::vector<uint8_t>& itm, ItemFacts* out);

constexpr uint16_t kResBam = 0x3E8, kResItm = 0x3ED, kResSpl = 0x3EE;

class GameRes {
 public:
  explicit GameRes(std::wstring game_dir) : dir_(std::move(game_dir)) {}
  std::vector<uint8_t> Read(const std::string& name, uint16_t type);
  // Cached: {w, h} of a BAM's first frame, {0, 0} when it cannot be read.
  std::pair<int, int> BamSize(const std::string& name);
  const SpellFacts* Spell(const std::string& name);  // nullptr when absent
  const ItemFacts* Item(const std::string& name);

 private:
  std::wstring dir_;
  bool key_loaded_ = false;
  KeyIndex key_;
  std::map<std::string, std::vector<uint8_t>> bifs_;
  std::map<std::string, std::pair<int, int>> bam_sizes_;
  std::map<std::string, SpellFacts> spells_;
  std::map<std::string, ItemFacts> items_;
  std::map<std::string, bool> missing_;
};

}  // namespace pstf
