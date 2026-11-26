// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Combat text ([tbc] combat_text), BG3 style (reference: youtube.com/watch?v=mfZz9N57glw, read at 5 and 60 fps):
// nothing rises. A line appears where the creature's head was (a fixed world point: the creature may walk away),
// sits there and fades out. Lines of one creature share that point and stack in slots from the top down: a new
// line takes the highest free slot ("Miss!" over the head, the damage row under it, a save line under that...).
// Damage is a bare number, white for weapon damage and coloured by damage type; numbers that land together share
// a row ("13 12"). "Miss!" or "Critical Miss!" (never both), "Critical Hit!" in red. A buff or debuff shows its name
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
constexpr uint32_t kCtJoinMs = 400;         // damage numbers of one creature this close share a row
constexpr int kCtLines = 24;                // on screen at once (UI menus PSTF_CT1..N)
constexpr int kCtSegs = 4;                  // numbers in one row
constexpr int kCtSlots = 6;                 // lines over one creature
constexpr int kCtLinePx = 26;               // UI px between slots
constexpr int kCtAboveHeadPx = 20;          // world px from the overhead-text point up to slot 0
constexpr int kCtNearX = 170, kCtNearY = 90;  // a new stack this close to another one shares its rows

enum class CtKind : uint8_t { Text, Crit, Miss, Damage, Heal, StatusOn, StatusOff };

// Text colours as the UI wants them: 0xBBGGRR.
constexpr uint32_t Bgr(int r, int g, int b) {
  return (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r);
}
constexpr uint32_t kCtWhite = Bgr(236, 232, 224);   // BG3's warm white
constexpr uint32_t kCtNumber = Bgr(255, 255, 255);  // weapon damage
constexpr uint32_t kCtRed = Bgr(214, 34, 30);       // "Concentration Broken!" / "Critical Hit!"
constexpr uint32_t kCtHeal = Bgr(112, 206, 92);

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
};
struct CtLine {
  int32_t id = -1;        // the creature it is about
  int ax = 0, ay = 0;     // world point of slot 0 (fixed when the creature's first line appeared)
  int slot = 0;
  uint32_t t0 = 0;        // ms when it appeared
  CtKind kind = CtKind::Text;
  std::vector<CtSeg> segs;
};

// The lines on screen. Pure: the module gives it times, ids and world points.
class CombatTextBook {
 public:
  // A new line over creature `id` whose head point is (x, y) now. Damage/heal numbers join that creature's newest
  // number row when it is younger than kCtJoinMs and has room. -> the line (or the row it joined).
  const CtLine& Add(int32_t id, int x, int y, CtKind kind, CtSeg seg, uint32_t now) {
    Expire(now);
    const bool number = kind == CtKind::Damage || kind == CtKind::Heal;
    if (number) {
      for (auto it = lines_.rbegin(); it != lines_.rend(); ++it) {
        if (it->id != id || (it->kind != CtKind::Damage && it->kind != CtKind::Heal)) continue;
        if (now - it->t0 < kCtJoinMs && static_cast<int>(it->segs.size()) < kCtSegs) {
          it->segs.push_back(std::move(seg));
          return *it;
        }
        break;
      }
    }
    CtLine l;
    l.id = id, l.kind = kind, l.t0 = now, l.ax = x, l.ay = y;
    bool own = false;
    for (const CtLine& o : lines_)
      if (o.id == id) l.ax = o.ax, l.ay = o.ay, own = true;  // the creature's stack keeps its point
    if (!own)  // a neighbour's stack close by: share its rows (its height), keep our own x
      for (const CtLine& o : lines_)
        if (std::abs(o.ax - x) < kCtNearX && std::abs(o.ay - y) < kCtNearY) {
          l.ay = o.ay;
          break;
        }
    uint32_t used = 0;
    for (const CtLine& o : lines_)
      if (o.id == id || (o.ay == l.ay && std::abs(o.ax - l.ax) < kCtNearX)) used |= 1u << o.slot;
    l.slot = FirstFree(used);
    if (l.slot < 0) {  // full: the oldest line of the rows gives way
      for (auto it = lines_.begin(); it != lines_.end(); ++it)
        if (it->id == id || (it->ay == l.ay && std::abs(it->ax - l.ax) < kCtNearX)) {
          l.slot = it->slot;
          lines_.erase(it);
          break;
        }
    }
    if (static_cast<int>(lines_.size()) >= kCtLines) lines_.erase(lines_.begin());
    l.segs.push_back(std::move(seg));
    lines_.push_back(std::move(l));
    return lines_.back();
  }
  void Expire(uint32_t now) {
    for (auto it = lines_.begin(); it != lines_.end();)
      it = now - it->t0 >= kCtLifeMs ? lines_.erase(it) : it + 1;
  }
  void Clear() { lines_.clear(); }
  const std::vector<CtLine>& Lines() const { return lines_; }
  static int FirstFree(uint32_t used) {
    for (int s = 0; s < kCtSlots; ++s)
      if (!(used & (1u << s))) return s;
    return -1;
  }

 private:
  std::vector<CtLine> lines_;  // oldest first
};

}  // namespace pstf::tbc
