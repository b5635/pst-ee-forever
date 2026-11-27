// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Combat text ([tbc] combat_text), BG3 style (reference: youtube.com/watch?v=mfZz9N57glw, read at 5 and 60 fps):
// nothing rises. A line appears where the creature's head was (a fixed world point: the creature may walk away),
// sits there and fades out. Lines of one creature share that point and stack upward from just above the head: a
// new line takes the lowest free slot, so nothing drops onto the body or the feet. A neighbour's lines count only
// when they would really run into ours (CtOverlapX on the text widths): then we share their rows, else creatures
// side by side keep their lines level over their own heads.
// Damage is a bare number, white for weapon damage and coloured by damage type, a heal a white "+N"; numbers glide
// out from the middle of the body and stop apart (kCtFan); one kind and colour within kCtMergeMs is one number (a
// dagger's 1d6 + 1 shows "7", not "6 1").
// "Miss!" or "Critical Miss!" (never both), "Critical Hit!" in red. A buff or debuff shows its name
// when it starts; when it ends the name shows again and a line is drawn through it from left to right.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace pstf::tbc {

constexpr uint32_t kCtFadeInMs = 120;
constexpr uint32_t kCtFadeOutMs = 2300;   // fully shown until here (the clip: ~2.4 s, then a 0.5 s fade)
constexpr uint32_t kCtLifeMs = 2800;
constexpr uint32_t kCtStrikeDelayMs = 300;  // a status that ends: shown plain, then struck through
constexpr uint32_t kCtStrikeMs = 400;       // the line crosses the name in this time
constexpr uint32_t kCtMergeMs = 100;        // numbers of one creature, kind and colour this close add up: 6 + 1 -> "7"
constexpr int kCtLines = 24;                // on screen at once (UI menus PSTF_CT1..N)
constexpr int kCtSegs = 4;                  // numbers in one row
// Damage numbers: PTSERIF. The game's VOLLKORN has old-style figures, its "1" is a short capital I.
constexpr const char* kCtNumFont = "PTSERIF";
constexpr int kCtSlots = 6;                 // lines over one creature
constexpr int kCtLinePx = 26;               // UI px between slots (slot n is n * this ABOVE slot 0)
constexpr int kCtAboveHeadPx = 20;          // world px from the overhead-text point up to slot 0
constexpr int kCtNearY = 90;  // a new stack this close in height to a line it would run into shares its rows

enum class CtKind : uint8_t { Text, Crit, Miss, Damage, Heal, StatusOn, StatusOff };

// Text colours as the UI wants them: 0xBBGGRR.
constexpr uint32_t Bgr(int r, int g, int b) {
  return (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r);
}
constexpr uint32_t kCtWhite = Bgr(236, 232, 224);   // BG3's warm white
constexpr uint32_t kCtNumber = Bgr(255, 255, 255);  // weapon damage
constexpr uint32_t kCtRed = Bgr(214, 34, 30);       // "Concentration Broken!" / "Critical Hit!"
constexpr uint32_t kCtHeal = Bgr(255, 255, 255);  // heals: white "+N"
inline std::string HealText(int v) { return "+" + std::to_string(v); }

// Damage and heal numbers (reference clip at 25-30 fps: the dagger's 12 at 0:10, Magic Missile's 3 5 3 at 0:41):
// a number appears at the middle of the body, glides out ~30 px with an ease-out in kCtDriftMs and stops, then sits
// until it fades. Numbers that come one after another each take another end spot round the body (kCtFan, the
// first free one), so where they stop they do not overlap. Offsets are world px at zoom 1.
constexpr uint32_t kCtDriftMs = 300;
constexpr int kCtFanN = 8;
// Round the body like a clock: up, the sides, short ones down (not onto the legs or a neighbour's head), then
// further out above. Each end spot is 26+ px (kCtNumH) in y or 38+ px in x from the others: two-digit numbers fit.
constexpr int kCtFan[kCtFanN][2] = {{0, -30}, {-38, -10}, {38, -10}, {24, 18}, {-24, 18}, {-30, -56}, {30, -56}, {0, -82}};
constexpr int kCtNumH = 26;  // a number's height: end spots closer than this in y and touching in x collide
// Share of the glide done at ms since the number appeared (ease-out cubic).
inline double CtDrift(uint32_t ms) {
  if (ms >= kCtDriftMs) return 1.0;
  const double t = 1.0 - static_cast<double>(ms) / kCtDriftMs;
  return 1.0 - t * t * t;
}

// Damage type bits (the damage effect's param2 high word, IESDP opcode 12) -> the number's colour.
// Weapon damage (crushing 0, piercing 0x10, slashing 0x100, missile 0x80) stays white.
inline uint32_t DamageColor(uint32_t bits) {
  if (bits & (0x8 | 0x200)) return Bgr(246, 124, 40);    // fire, magic fire
  if (bits & (0x2 | 0x400)) return Bgr(124, 204, 246);   // cold, magic cold
  if (bits & 0x4) return Bgr(86, 146, 255);              // electricity
  if (bits & 0x1) return Bgr(176, 214, 44);              // acid
  if (bits & 0x20) return Bgr(96, 184, 64);              // poison
  if (bits & 0x40) return Bgr(214, 72, 60);              // magic (BG3 force)
  if (bits & 0x800) return Bgr(156, 112, 224);           // stunning
  return kCtNumber;
}

// What an attack roll shows over its target: "" for a plain hit (its damage number says it), "Miss!" or
// "Critical Miss!" (one of them), "Critical Hit!". Blocked / takedown outcomes (1, 2) have their own lines.
inline const char* AttackText(bool hit, bool crit, int roll, int outcome) {
  if (outcome != 0) return "";
  if (!hit) return roll == 1 ? "Critical Miss!" : "Miss!";
  return crit || roll == 20 ? "Critical Hit!" : "";
}
inline CtKind AttackKind(const char* text) {
  return std::strncmp(text, "Critical", 8) == 0 ? CtKind::Crit : CtKind::Miss;
}

// Trip / Knockdown: the swing missed, it hit and the target saved, or the target went down.
enum class TakedownResult : uint8_t { Missed, Failed, Success };
inline std::string TakedownText(const char* name, TakedownResult r) {
  return std::string(name) + (r == TakedownResult::Missed ? ": Missed" : r == TakedownResult::Failed ? ": Failed" : ": Success");
}

// The engine's rising messages: "Damage: 7" (damage effects), "Heal: 3" (heals, and a heal through a damage
// effect in PST), anything else (spell names, script text) as it is. -> kind and, for numbers, the value.
struct FloaterParse {
  CtKind kind = CtKind::Text;
  int value = 0;
};
inline FloaterParse ParseFloater(const std::string& text, bool in_damage) {
  FloaterParse p;
  size_t end = text.size();
  while (end > 0 && text[end - 1] == ' ') --end;
  size_t start = end;
  while (start > 0 && text[start - 1] >= '0' && text[start - 1] <= '9') --start;
  const bool number = start < end && start > 0 && (text[start - 1] == ' ' || text[start - 1] == ':');
  if (!number) return p;
  const int v = std::atoi(text.c_str() + start);
  if (text.compare(0, 4, "Heal") == 0) {
    p.kind = CtKind::Heal, p.value = v;
  } else if (in_damage) {
    p.kind = CtKind::Damage, p.value = v;
  }
  return p;
}

// The engine's feedback lines over a creature lose its name (" Casts Magic Missile : Morte"): trimmed; a cast
// keeps only the spell's name, as BG3 names the action.
inline std::string CleanFloaterText(const std::string& text) {
  size_t a = 0, b = text.size();
  while (a < b && (text[a] == ' ' || text[a] == '	')) ++a;
  while (b > a && (text[b - 1] == ' ' || text[b - 1] == '	')) --b;
  std::string t = text.substr(a, b - a);
  if (t.compare(0, 6, "Casts ") == 0) {
    const size_t c = t.find(" : ");
    t = t.substr(6, c == std::string::npos ? std::string::npos : c - 6);
  }
  return t;
}

// Opacity of a line (all its parts), 0..1.
inline double CtAlpha(uint32_t ms) {
  if (ms >= kCtLifeMs) return 0.0;
  if (ms < kCtFadeInMs) return static_cast<double>(ms) / kCtFadeInMs;
  if (ms >= kCtFadeOutMs) return static_cast<double>(kCtLifeMs - ms) / (kCtLifeMs - kCtFadeOutMs);
  return 1.0;
}
// How much of an ended status's name is struck through, 0..1.
inline double CtStrike(uint32_t ms) {
  if (ms <= kCtStrikeDelayMs) return 0.0;
  const uint32_t t = ms - kCtStrikeDelayMs;
  return t >= kCtStrikeMs ? 1.0 : static_cast<double>(t) / kCtStrikeMs;
}

struct CtSeg {
  std::string text;  // "@123" = TLK strref 123 (the UI fetches it)
  uint32_t color = kCtWhite;
  int value = -1;    // a damage/heal number (>= 0): numbers of one colour close together add up
  uint32_t t = 0;    // ms when it appeared (set by Add)
};
struct CtLine {
  int32_t id = -1;        // the creature it is about
  int ax = 0, ay = 0;     // world point of slot 0 (fixed when the creature's first line appeared); a number: its start
  int ex = 0, ey = 0;     // a number: where it glides to, from (ax, ay) (kCtFan)
  int slot = 0;           // 0 just above the head; higher slots stack upward (kCtLinePx each)
  int w = 0;              // estimated width (CtLineWidth), world px at zoom 1
  uint32_t t0 = 0;        // ms when it appeared
  CtKind kind = CtKind::Text;
  std::vector<CtSeg> segs;
};

// A line's width as drawn (18 pt VOLLKORN text ~9 px a character, 26 pt numbers ~14 px a digit, 3 px between
// numbers; a TLK strref "@N" counts as 12 characters). An estimate: it only decides whether lines collide.
constexpr int kCtCharPx = 9, kCtDigitPx = 14, kCtNumGapPx = 3, kCtPadPx = 10;
inline int CtLineWidth(CtKind kind, const std::vector<CtSeg>& segs) {
  int w = 0;
  const bool num = kind == CtKind::Damage || kind == CtKind::Heal;
  for (size_t i = 0; i < segs.size(); ++i) {
    const std::string& t = segs[i].text;
    const int chars = !t.empty() && t[0] == '@' ? 12 : static_cast<int>(t.size());
    w += chars * (num ? kCtDigitPx : kCtCharPx) + (i > 0 ? kCtNumGapPx : 0);
  }
  return w;
}
// Two lines centred at ax with widths wa and bx, wb collide (with kCtPadPx air between them).
inline bool CtOverlapX(int ax, int wa, int bx, int wb) { return 2 * std::abs(ax - bx) < wa + wb + 2 * kCtPadPx; }

// The lines on screen. Pure: the module gives it times, ids and world points.
class CombatTextBook {
 public:
  // A new line over creature `id`: (x, y) is its head point now for text, the middle of its body for a damage/heal
  // number (AddNumber; Merged() says it was added to an earlier number). -> the line (or the number it joined).
  const CtLine& Add(int32_t id, int x, int y, CtKind kind, CtSeg seg, uint32_t now) {
    Expire(now);
    merged_ = false;
    seg.t = now;
    if (IsNumber(kind)) return AddNumber(id, x, y, kind, std::move(seg), now);
    CtLine l;
    l.id = id, l.kind = kind, l.t0 = now, l.ax = x, l.ay = y;
    l.segs.push_back(std::move(seg));
    l.w = CtLineWidth(kind, l.segs);
    bool own = false;
    for (const CtLine& o : lines_)
      if (o.id == id && !IsNumber(o.kind)) l.ax = o.ax, l.ay = o.ay, own = true;  // the creature's stack keeps its point
    // A neighbour's line close in height that the new line would run into: share its rows (its height), keep our
    // own x. Neighbours whose lines do not touch ours keep their own heights and slots.
    // (numbers glide on their own and take no slot)
    auto collides = [&](const CtLine& o) { return !IsNumber(o.kind) && o.ay == l.ay && CtOverlapX(o.ax, o.w, l.ax, l.w); };
    if (!own)
      for (const CtLine& o : lines_)
        if (!IsNumber(o.kind) && std::abs(o.ay - y) < kCtNearY && CtOverlapX(o.ax, o.w, l.ax, l.w)) {
          l.ay = o.ay;
          break;
        }
    uint32_t used = 0;
    for (const CtLine& o : lines_)
      if ((o.id == id && !IsNumber(o.kind)) || collides(o)) used |= 1u << o.slot;
    l.slot = FirstFree(used);
    if (l.slot < 0) {  // full: the oldest line of the rows gives way
      for (auto it = lines_.begin(); it != lines_.end(); ++it)
        if ((it->id == id && !IsNumber(it->kind)) || collides(*it)) {
          l.slot = it->slot;
          lines_.erase(it);
          break;
        }
    }
    if (static_cast<int>(lines_.size()) >= kCtLines) lines_.erase(lines_.begin());
    lines_.push_back(std::move(l));
    return lines_.back();
  }
  void Expire(uint32_t now) {
    for (auto it = lines_.begin(); it != lines_.end();)
      it = now - it->t0 >= kCtLifeMs ? lines_.erase(it) : it + 1;
  }
  void Clear() { lines_.clear(); }
  const std::vector<CtLine>& Lines() const { return lines_; }
  bool Merged() const { return merged_; }  // the last Add added its number to an earlier one
  static bool IsNumber(CtKind k) { return k == CtKind::Damage || k == CtKind::Heal; }
  // Where a line is drawn at `now`, world px: a number glides from its start toward its end spot; a text line sits
  // at its stack point (the caller adds its slot).
  static void Position(const CtLine& l, uint32_t now, int* x, int* y) {
    const double d = IsNumber(l.kind) ? CtDrift(now - l.t0) : 0.0;
    *x = l.ax + static_cast<int>(l.ex * d + (l.ex >= 0 ? 0.5 : -0.5));
    *y = l.ay + static_cast<int>(l.ey * d + (l.ey >= 0 ? 0.5 : -0.5));
  }
  static int FirstFree(uint32_t used) {
    for (int s = 0; s < kCtSlots; ++s)
      if (!(used & (1u << s))) return s;
    return -1;
  }

 private:
  // A damage/heal number over creature `id` whose body middle is (x, y) now. The same kind and colour within
  // kCtMergeMs adds to the earlier number (a dagger's 6 + 1 shows 7; heals show "+N"). Otherwise it is a new line that
  // glides to the first spot of kCtFan whose end does not touch another number's end (any creature's).
  const CtLine& AddNumber(int32_t id, int x, int y, CtKind kind, CtSeg seg, uint32_t now) {
    const bool heal = kind == CtKind::Heal;
    if (seg.value >= 0)
      for (auto it = lines_.rbegin(); it != lines_.rend(); ++it) {
        if (it->id != id || it->kind != kind || it->segs.empty()) continue;
        CtSeg& s = it->segs[0];
        if (s.value >= 0 && s.color == seg.color && now - s.t < kCtMergeMs) {
          s.value += seg.value;
          s.text = heal ? HealText(s.value) : std::to_string(s.value);
          it->w = CtLineWidth(it->kind, it->segs);
          merged_ = true;
          return *it;
        }
      }
    if (heal && seg.value >= 0) seg.text = HealText(seg.value);
    CtLine l;
    l.id = id, l.kind = kind, l.t0 = now, l.ax = x, l.ay = y;
    l.segs.push_back(std::move(seg));
    l.w = CtLineWidth(kind, l.segs);
    int best = -1, best_hits = 1 << 30;
    for (int k = 0; k < kCtFanN; ++k) {
      const int ex = x + kCtFan[k][0], ey = y + kCtFan[k][1];
      int hits = 0;
      for (const CtLine& o : lines_) {
        if (!IsNumber(o.kind)) continue;
        if (std::abs(o.ay + o.ey - ey) < kCtNumH && CtOverlapX(o.ax + o.ex, o.w, ex, l.w)) ++hits;
      }
      if (hits < best_hits) best_hits = hits, best = k;
      if (hits == 0) break;
    }
    l.ex = kCtFan[best][0], l.ey = kCtFan[best][1];
    if (static_cast<int>(lines_.size()) >= kCtLines) lines_.erase(lines_.begin());
    lines_.push_back(std::move(l));
    return lines_.back();
  }

  std::vector<CtLine> lines_;  // oldest first
  bool merged_ = false;
};

}  // namespace pstf::tbc
