// SPDX-License-Identifier: GPL-2.0-or-later
// Status icons (BG3 style, as in our GemRB fork's CollectStatus): one icon per spell or item whose
// timed effects are on a creature, with the rounds left and the share of its duration still to run
// (drawn as a frame around the icon that drains counterclockwise from the top centre). Pure logic.
#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "modules/tbc/round_rules.h"

namespace pstf::tbc {

struct StatusEffect {
  uint32_t parent_type = 0;  // 1 spell, 2 item (EFF v2 0x90)
  std::string parent;        // resref (0x94)
  uint32_t parent_flags = 0; // the SPL/ITM flags (0x9C); bit 10 = hostile
  int32_t left = 0;          // ticks to expiry; <= 0 or not timed: not a status
  bool timed = false;
  bool delayed = false;      // not running yet (a spell on its way)
  uint64_t key = 0;          // the effect's identity while it lives
  int opcode = -1;           // -1 unknown (an attached spell object)
};

// Effects a spell adds through the engine's own code carry no parent: Submerge the Will's AC and saves
// (opcode 375, CGameEffectAddAttachedObject builds them with DecodeEffect and no source), Power of One's
// strength (projectile 287). They belong to the spell last seen on its way to that creature: a delayed
// effect with a parent, seen within kOrphanWindowTicks. Once given, an effect keeps its parent.
struct OrphanMap {
  struct Parent {
    std::string res;
    uint32_t type = 0, flags = 0;
    int64_t seen = 0;
  };
  std::map<int32_t, Parent> recent;                      // creature -> last spell on its way
  std::map<int32_t, std::map<uint64_t, Parent>> given;   // creature -> effect key -> its parent
};
constexpr int64_t kOrphanWindowTicks = 150;
inline void AttributeOrphans(int32_t id, std::vector<StatusEffect>* fx, int64_t now, OrphanMap* m) {
  for (const StatusEffect& e : *fx)
    if (e.delayed && !e.parent.empty() && (e.parent_type == 1 || e.parent_type == 2))
      m->recent[id] = {e.parent, e.parent_type, e.parent_flags, now};
  std::map<uint64_t, OrphanMap::Parent>& mine = m->given[id];
  std::map<uint64_t, OrphanMap::Parent> keep;
  const auto rec = m->recent.find(id);
  for (StatusEffect& e : *fx) {
    if (!e.timed || !e.parent.empty() || !e.key) continue;
    const auto it = mine.find(e.key);
    OrphanMap::Parent p;
    if (it != mine.end()) {
      p = it->second;
    } else if (rec != m->recent.end() && now - rec->second.seen <= kOrphanWindowTicks) {
      p = rec->second;
    } else {
      continue;
    }
    keep[e.key] = p;
    e.parent = p.res, e.parent_type = p.type, e.parent_flags = p.flags;
  }
  mine.swap(keep);  // effects that are gone forget their parent (their keys may be reused)
}

struct StatusIcon {
  std::string parent;
  uint32_t parent_type = 0;
  int rounds = 0;      // rounds left, rounded up
  double share = 1.0;  // of its duration still to run
  bool debuff = false;
};

// Remembers how long each (creature, source) span was when first seen; a recast (later end) restarts it.
struct StatusSpans {
  struct Span {
    int64_t end = 0;
    int32_t length = 1;
  };
  std::map<std::pair<int32_t, std::string>, Span> spans;
};

constexpr uint32_t kHostileFlag = 0x400;

inline std::vector<StatusIcon> CollectStatus(int32_t id, const std::vector<StatusEffect>& fx, int64_t now,
                                             int round_ticks, StatusSpans* spans, size_t max_icons) {
  struct Src {
    std::string res;
    uint32_t type = 0, flags = 0;
    int32_t left = 0;
  };
  std::vector<Src> src;
  if (round_ticks <= 0) round_ticks = 100;
  for (const StatusEffect& e : fx) {
    if (!e.timed || e.parent.empty() || (e.parent_type != 1 && e.parent_type != 2)) continue;
    // flashes, sounds: not a status. A quarter round, not half: Globe of Invulnerability's sphere re-applies
    // its 3 s (45 tick) protections while the caster stands in it.
    if (e.left < round_ticks / 4) continue;
    // a glow, colour pulse or sound only shows the spell: Blessing's glow (op 9, 32 s) outlasts its 30 s bless
    // and made the icon a round too long; a spell with nothing but such effects left has no status
    if (IsPresentationOpcode(e.opcode)) continue;
    auto it = std::find_if(src.begin(), src.end(), [&](const Src& s) { return s.res == e.parent; });
    if (it == src.end()) {
      src.push_back({e.parent, e.parent_type, e.parent_flags, e.left});
    } else {
      it->left = std::max(it->left, e.left);
      it->flags |= e.parent_flags;
    }
  }
  std::vector<StatusIcon> out;
  for (const Src& s : src) {
    StatusIcon icon;
    icon.parent = s.res;
    icon.parent_type = s.type;
    icon.rounds = (s.left + round_ticks - 1) / round_ticks;
    icon.debuff = (s.flags & kHostileFlag) != 0;
    if (spans) {
      auto& span = spans->spans[{id, s.res}];
      const int64_t end = now + s.left;
      if (span.end == 0 || end > span.end + 1) {  // first seen or recast
        span.end = end;
        span.length = std::max<int32_t>(1, s.left);
      }
      icon.share = std::clamp(double(s.left) / double(span.length), 0.0, 1.0);
    }
    out.push_back(icon);
  }
  // debuffs first, then the ones running out soonest
  std::stable_sort(out.begin(), out.end(), [](const StatusIcon& a, const StatusIcon& b) {
    if (a.debuff != b.debuff) return a.debuff;
    return a.rounds < b.rounds;
  });
  if (out.size() > max_icons) out.resize(max_icons);
  return out;
}

// The drained frame: up to five rectangles (x, y, w, h) covering `share` of the outline of the box
// (x, y, size, size), `thick` px wide, from the top centre counterclockwise (left along the top, down
// the left side, along the bottom, up the right side, back left to the centre).
struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
};
inline std::vector<Rect> DrainFrame(int x, int y, int size, int thick, double share) {
  std::vector<Rect> r;
  if (share <= 0 || size < 2 * thick) return r;
  share = std::min(share, 1.0);
  const int side = size - thick;           // each leg's length along the outline (corners counted once)
  const int perimeter = 4 * side;
  int left = static_cast<int>(share * perimeter + 0.5);
  const int half = side / 2;
  // leg 1: top, from the centre to the left corner
  int n = std::min(left, half);
  if (n > 0) r.push_back({x + half - n + thick / 2, y, n, thick});
  left -= n;
  // leg 2: left side, top to bottom
  n = std::min(left, side);
  if (n > 0) r.push_back({x, y, thick, std::min(size, n + thick)});
  left -= n;
  // leg 3: bottom, left to right
  n = std::min(left, side);
  if (n > 0) r.push_back({x, y + size - thick, std::min(size, n + thick), thick});
  left -= n;
  // leg 4: right side, bottom to top
  n = std::min(left, side);
  if (n > 0) r.push_back({x + size - thick, y + size - std::min(size, n + thick), thick, std::min(size, n + thick)});
  left -= n;
  // leg 5: top, from the right corner back to the centre
  n = std::min(left, side - half);
  if (n > 0) r.push_back({x + size - thick - n, y, n + thick, thick});
  return r;
}

// An icon of w x h fitted into a box of `box` px, aspect kept, centred: {x, y, w, h} offsets.
inline Rect FitIcon(int w, int h, int box) {
  if (w <= 0 || h <= 0) return {0, 0, box, box};
  const double s = std::min(double(box) / w, double(box) / h);
  const int fw = std::max(1, static_cast<int>(w * s + 0.5)), fh = std::max(1, static_cast<int>(h * s + 0.5));
  return {(box - fw) / 2, (box - fh) / 2, fw, fh};
}

}  // namespace pstf::tbc
