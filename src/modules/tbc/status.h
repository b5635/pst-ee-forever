// SPDX-License-Identifier: GPL-2.0-or-later
// Status icons (BG3 style, as in our GemRB fork's CollectStatus): one icon per spell or item whose
// timed effects are on a creature, with the rounds left and the share of its duration still to run
// (drawn as a frame around the icon that drains counterclockwise from the top centre). Pure logic.
#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
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
  bool orphan = false;       // its parent was given by AttributeOrphans (an engine-built effect)
};

// Effects a spell adds through the engine's own code carry no parent: Submerge the Will's AC and saves
// (opcode 375, CGameEffectAddAttachedObject builds them with DecodeEffect and no source), Power of One's
// strength (projectile 287), Halo of Lesser Revelation's AC (projectile 259). They belong to the spell last seen
// arriving on that creature: a delayed effect with a parent, or a timed one with a parent that was not there at the
// previous look (Halo brings only its glow, op 9, 3 s), seen within kOrphanWindowTicks. Once given, an effect keeps
// its parent. Only an orphan that is new at this look takes a parent (one the creature already had is no one's:
// Blindness took a 50-round leftover); a creature's first look marks nothing as arriving.
struct OrphanMap {
  struct Parent {
    std::string res;
    uint32_t type = 0, flags = 0;
    int64_t seen = 0;
    int op = -2;       // the effect it was given to: a reused address with another opcode, or more time left than
    int32_t left = 0;  // at the last look, is a new effect (Black-Barbed Shield's AC took Submerge the Will's parent)
  };
  struct Seen {
    int op = -2;
    int32_t left = 0;
  };
  // the same effect as at the last look: same opcode and its time has not grown (the engine reuses freed memory)
  static bool Same(int op0, int32_t left0, const StatusEffect& e) { return op0 == e.opcode && e.left <= left0 + 1; }
  std::map<int32_t, Parent> recent;                      // creature -> last spell on its way
  std::map<int32_t, std::map<uint64_t, Parent>> given;   // creature -> effect key -> its parent
  std::map<int32_t, std::set<uint64_t>> parented;        // creature -> keys of its parented effects at the last look
  std::map<int32_t, std::map<uint64_t, Seen>> orphans;   // creature -> its parentless effects at the last look
  Parent cast;  // the spell being cast in the fight (any caster), last frame it was seen: a fallback parent
  // Halo of Lesser Revelation and Black-Barbed Shield bring only parentless effects (their projectiles build them):
  // with no parented effect arriving, a new orphan takes the spell being cast (CGameSprite +0x47F8, engine::CurrentSpell).
  void NoteCast(const std::string& res, uint32_t flags, int64_t now) { cast = {res, 1, flags, now}; }
};
constexpr int64_t kOrphanWindowTicks = 150;
inline void AttributeOrphans(int32_t id, std::vector<StatusEffect>* fx, int64_t now, OrphanMap* m) {
  const auto seen = m->parented.find(id);
  const bool looked = seen != m->parented.end();
  std::set<uint64_t> keys;
  for (const StatusEffect& e : *fx) {
    if (e.parent.empty() || (e.parent_type != 1 && e.parent_type != 2)) continue;
    if (e.key) keys.insert(e.key);
    const bool arrived = e.timed && e.key && looked && !seen->second.count(e.key);
    if (e.delayed || arrived) m->recent[id] = {e.parent, e.parent_type, e.parent_flags, now};
  }
  m->parented[id].swap(keys);
  const auto old = m->orphans.find(id);
  const bool had_look = old != m->orphans.end();
  std::map<uint64_t, OrphanMap::Seen> orphan_keys;
  for (const StatusEffect& e : *fx)
    if (e.timed && e.parent.empty() && e.key) orphan_keys[e.key] = {e.opcode, e.left};
  std::map<uint64_t, OrphanMap::Parent>& mine = m->given[id];
  std::map<uint64_t, OrphanMap::Parent> keep;
  const auto rec = m->recent.find(id);
  for (StatusEffect& e : *fx) {
    if (!e.timed || !e.parent.empty() || !e.key) continue;
    const auto it = mine.find(e.key);
    bool arrived = false;
    if (had_look) {
      const auto was = old->second.find(e.key);
      arrived = was == old->second.end() || !OrphanMap::Same(was->second.op, was->second.left, e);
    }
    OrphanMap::Parent p;
    if (it != mine.end() && OrphanMap::Same(it->second.op, it->second.left, e)) {
      p = it->second;
    } else if (arrived && rec != m->recent.end() && now - rec->second.seen <= kOrphanWindowTicks) {
      p = rec->second;
    } else if (arrived && !m->cast.res.empty() && now - m->cast.seen <= kOrphanWindowTicks) {
      p = m->cast;
    } else {
      continue;
    }
    p.op = e.opcode, p.left = e.left;
    keep[e.key] = p;
    e.parent = p.res, e.parent_type = p.type, e.parent_flags = p.flags;
    e.orphan = true;
  }
  mine.swap(keep);  // effects that are gone forget their parent (their keys may be reused)
  m->orphans[id].swap(orphan_keys);
}

struct StatusIcon {
  std::string parent;
  uint32_t parent_type = 0;
  int rounds = 0;      // rounds left, rounded up
  double share = 1.0;  // of its duration still to run
  bool debuff = false;
};

// Remembers how long each (creature, source) span was when first seen (its length at the cast); a recast restarts
// it. A recast is the time left going UP: in a turn-based fight the round clock holds effect time still while the
// world clock runs (left stays, now grows), so "now + left" grows every frame and must not count as a recast (it
// did: the drain frame refilled between turns and the length restarted from what was left).
struct StatusSpans {
  struct Span {
    int32_t last_left = -1;  // -1: not seen yet
    int32_t length = 1;
  };
  std::map<std::pair<int32_t, std::string>, Span> spans;
};

constexpr uint32_t kHostileFlag = 0x400;
// Effects "until destroyed" (Armor's AC, op 375 type 7) run ~INT_MAX ticks: no countdown (rounds 0), a full frame.
// (left + round - 1) overflowed int32 and showed r-28633114.
constexpr int32_t kNoEndTicks = 15 * 60 * 60 * 24;  // a day of world time

inline std::vector<StatusIcon> CollectStatus(int32_t id, const std::vector<StatusEffect>& fx, int64_t now,
                                             int round_ticks, StatusSpans* spans, size_t max_icons) {
  struct Src {
    std::string res;
    uint32_t type = 0, flags = 0;
    int32_t left = 0;     // longest of its own timed effects
    int32_t attached = 0; // longest of its attached spell objects (opcode -1)
    int32_t orphan = 0;   // longest of its engine-built (parentless) timed effects
  };
  (void)now;  // spans follow the time left, not the world clock (frozen in a fight)
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
    if (it == src.end()) it = src.insert(src.end(), {e.parent, e.parent_type, e.parent_flags, 0, 0, 0});
    // An attached spell object counts only for a spell with no timed effect of its own: Friends' sparkle (its op
    // 369 visual, an attached object) outlived its CHR bonus by a round and held the icon (spell_follow, 2026-11-25);
    // Submerge the Will's and the shields' objects carry their spell when the effects are short re-applies.
    // Antimagic Shell's object re-applies 60-tick parentless resistances: those never decide against the object's own
    // 5 minutes (the icon showed r1 while they were up, r59 between; spell_follow -During, 2026-11-25).
    int32_t& slot = e.opcode == -1 ? it->attached : e.orphan ? it->orphan : it->left;
    slot = std::max(slot, e.left);
    it->flags |= e.parent_flags;
  }
  std::vector<StatusIcon> out;
  for (Src s : src) {
    if (s.left <= 0) s.left = std::max(s.orphan, s.attached);
    StatusIcon icon;
    icon.parent = s.res;
    icon.parent_type = s.type;
    const bool no_end = s.left >= kNoEndTicks;
    icon.rounds = no_end ? 0 : static_cast<int>((int64_t(s.left) + round_ticks - 1) / round_ticks);
    icon.debuff = (s.flags & kHostileFlag) != 0;
    if (spans) {
      auto& span = spans->spans[{id, s.res}];
      if (span.last_left < 0 || s.left > span.last_left + 1) span.length = std::max<int32_t>(1, s.left);  // first / recast
      span.last_left = s.left;
      icon.share = no_end ? 1.0 : std::clamp(double(s.left) / double(span.length), 0.0, 1.0);
    }
    out.push_back(icon);
  }
  // debuffs first, then the ones running out soonest
  std::stable_sort(out.begin(), out.end(), [](const StatusIcon& a, const StatusIcon& b) {
    if (a.debuff != b.debuff) return a.debuff;
    return (a.rounds ? a.rounds : INT32_MAX) < (b.rounds ? b.rounds : INT32_MAX);  // no end: last
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
