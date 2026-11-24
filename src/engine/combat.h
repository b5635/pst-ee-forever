// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter for turn-based combat rules and presentation (symbols.h "round clock for effects",
// "combat presentation and pacing"). Game thread only; callers pass creature ids, never pointers.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pstf::engine {

// --- round clock for effects ---------------------------------------------------------------------
// Persistent effects (poison, regeneration, damage over time) of creatures for which `frozen(id)`
// is true never advance on their own: the engine's gate in ProcessEffectList answers "skip".
using FrozenFn = bool (*)(int32_t id);
bool InstallEffectClock(FrozenFn frozen);
bool EffectClockInstalled();
// Keeps a creature's own timers where they are while `ticks` of world time pass: absolute and
// delayed effect ticks and the natural-regeneration stamp move forward by `ticks`.
void ShiftEffectClock(int32_t id, int32_t ticks);
// PST attached spell objects on the creature (symbols.h kOffSpriteAttached): count; `left` gets ticks to
// each one's end (0 = no end). The round clock moves their ends with the effects.
int AttachedLeft(int32_t id, std::vector<int32_t>* left);
// Delayed effects that `resolves_now(ticks to onset)` says are part of the spell resolving keep running
// on world time (never shifted) until they fire; ResolvingOnsets counts those still pending.
// resolves_now(ticks to onset, opcode): 0 = round-clocked, 1 = the spell resolving (world time, the
// caster's turn waits for it), 2 = presentation only (world time, nobody waits).
using OnsetFn = int (*)(int32_t left, int opcode);
void SetOnsetRule(OnsetFn resolves_now);
int ResolvingOnsets(int32_t id);
void ForgetOnsets();  // fight over
// One round of the creature's own time passes now (at its turn start): timed effects come `ticks`
// closer to their end, natural regeneration accrues `ticks`, persistent effects run `ticks` at once
// (the engine's own catch-up, as CompressTime does), and the effect list is re-evaluated at once.
void StepEffectClock(int32_t id, uint32_t ticks);

// A held creature's AI tick: its effect list runs (spells cast on it land at once), as ProcessAI
// would run it, without its scripts or actions. Call from the ProcessAI hook instead of ProcessAI.
void HeldEffectsTick(void* sprite);

// SetInterrupt(FALSE) the engine's way: its scripts leave the current/queued actions alone until the
// queue runs empty (the engine then clears it by itself).
void SetNoInterrupt(int32_t id);

struct EffectView {
  int opcode = 0;
  uint32_t type = 0;
  int32_t left = 0;  // ticks to expiry (absolute/delayed types), else 0
};
std::vector<EffectView> TimedEffects(int32_t id);
// Timed effects with their parent spell/item (status icons).
struct ParentEffectView {
  uint32_t parent_type = 0, parent_flags = 0;
  char parent[9] = {};
  int32_t left = 0;  // ticks to expiry (delayed: to onset)
  bool timed = false;
  bool delayed = false;  // delayed types 6/7/8: not running yet (their parent names a spell on its way)
  uint64_t key = 0;      // identity of the effect while it lives (compare only)
};
std::vector<ParentEffectView> ParentEffects(int32_t id);
// The creature's colour glow effects (opcodes 8, 9, 52, 61, 353 tint screen; symbols.h "Colour effects").
struct ColorEffectView {
  int opcode = 0;
  uint32_t param1 = 0, param2 = 0;  // param1 = colour 0xBBGGRR00
  uint32_t timing = 0;              // effect timing mode (2 = while equipped: a magic weapon's glow)
  uint64_t key = 0;                 // identity of the effect while it lives (compare only)
};
std::vector<ColorEffectView> ColorEffects(int32_t id);
int PersistentCount(int32_t id);

// Every projectile's AIUpdate asks `run(caster id, projectile id, lingers)` first and is skipped on
// false (vtable slot 3 of each projectile class met in the area is redirected, after checking it is
// an engine CGameObject vtable). `lingers`: an area cloud or Swarm Curse that has landed and now
// pulses its effects on a tick countdown.
using ProjGateFn = bool (*)(int32_t caster, int32_t projectile, bool lingers);
void SetProjectileGate(ProjGateFn run);
bool ProjectileLingers(int32_t projectile);
void WatchProjectiles();  // call per frame: hooks the vtables of new projectiles

// --- pacing --------------------------------------------------------------------------------------
// Everyone runs: PST character animations through their own SetRunning(1); other animations get
// the run speed on their move scale. `restore` puts the walk speed back.
void ForceRun(int32_t id, bool run, uint8_t run_scale_other);
uint8_t MoveScale(int32_t id);  // current move scale (GetMoveScale)

// --- sprite update, poses, bumping (symbols.h "Per-tick sprite update", "AnimPst.vtbl.SetSequence") -----
// CGameSprite::AIUpdate (vtable slot 3) goes through `fn(sprite, id, orig)`: it calls orig 0, 1 or more times.
using SpriteUpdateFn = void (*)(void* sprite, int32_t id, void (*orig)(void*));
bool InstallSpriteUpdate(SpriteUpdateFn fn);
// Bumping: `guard(id)` true = a walker may not push this creature aside (as the engine never pushes enemies).
using BumpGuardFn = bool (*)(int32_t id);
bool InstallBumpGuard(BumpGuardFn guard);
int16_t SpriteSequence(int32_t id);              // CGameSprite sequence word (-1: none)
bool InAttackRound(int32_t id);                  // its melee reach has the engine's RANGE_FUDGE (+1 cell) now
void KeepAttackRound(int32_t id);                // sets that flag again (after a ClearAI that cleared it)
int AttackRoundPos(int32_t id);                  // ticks into its attack round (-1 = a new one at the next swing)
void NewAttackRound(int32_t id);                 // its next swing starts a fresh attack round (swings soon, not next round)
bool SetSpriteSequence(int32_t id, int16_t seq); // CGameSprite::SetSequence (sounds and all, as the engine)
inline bool IsAttackSequence(int16_t s) { return s == 0 || s == 8 || s == 11 || s == 12 || s == 13; }
// Lying down: a PST character animation shows its die cycle at `back` frames before the last one (0 = on the
// ground; more = on the way up or down). Returns the cycle's frame count (0: not a PST animation).
int PoseLying(int32_t id, int back);
// The engine's armour hit sound at the creature (HIT_02<armor>, as a piercing hit plays it).
bool PlayArmorHitSound(int32_t id);
// Anim probe: run flag, cycle word, frame, sprite sequence (-1s when not a PST animation).
std::string AnimProbe(int32_t id);

// Keeps the battle song playing in the area the creature is in (the engine's own counter).
void KeepBattleMusic(int32_t id, int ticks);

// Camera: the view's top left in world pixels (false outside an area) and moving it.
// Line of sight in a fight: in the area `anchor` (a party member) stands in, the vis-map tiles within
// `radius` px of each creature in `ids` become explored and visible (slot-14 bit, not a party
// member's), so the party sees every combatant; tiles marked by the previous call and not by this one
// lose that bit again. An empty list clears all.
void RevealCombatants(int32_t anchor, const std::vector<int32_t>& ids, int radius);

// Item use: true once for each creature whose UseItem/UseItemPoint action ran since the last call.
bool InstallItemUseHooks();
bool TookItemAction(int32_t id);

// The engine's cursor mode (CInfGame state word): 0 = normal, other = aiming a spell, ability or item.
int GameCursorState();
bool ViewCenter(int* x, int* y);
void CenterView(int x, int y);

// --- rolls ---------------------------------------------------------------------------------------
struct RollEvent {
  enum Kind : uint8_t { Attack, Save } kind = Attack;
  int32_t actor = -1;   // attacker / the creature that saves
  int32_t target = -1;  // attack target
  int roll = 0;         // the d20
  int total = 0;        // attack: roll + bonuses
  int needs = 0;        // attack: THAC0 - AC of the target (the total that hits)
  bool success = false; // hit / saved
  int save_type = 0;    // bit index of the save (0 spell .. 4 polymorph)
  uint8_t outcome = 0;  // attack: 0 as the engine rolled, 1 blocked, 2 takedown (hit, no damage)
  uint8_t site = 0;     // attack: which of Swing's Hit calls rolled it (1..3, symbols.h Swing.hitN)
};
bool InstallRollHooks();
// Test helpers (all-spells suite): remove a spell's effects from a creature (CGameSprite::UnapplySpell; -> effects
// removed, -1 unavailable) and end its PST attached spell objects (shields, mantles) on the next tick.
int UnapplySpell(int32_t id, const char* res);
int EndAttached(int32_t id);
// CGameSprite::FeedBack hook: counts each creature's casts (feedback 5, sent by Spell/SpellPoint/SpellPST/
// SpellPointPST when the spell goes off); `log` = also log every FeedBack (debug, env PSTF_FEEDBACK_LOG=1).
bool InstallFeedbackHook(bool log);
bool InstallFeedbackLog();   // InstallFeedbackHook(true)
int SpellCasts(int32_t id);  // spells the engine cast for it since the hook went on; -1 without the hook
std::vector<RollEvent> TakeRollEvents();

// Melee swings (Swing's first Hit call, the melee branch): `thac0_add(attacker, target)` is added to the
// attacker's THAC0 for that one roll (trip: +2); after the engine's roll, `outcome(attacker, target, hit,
// d20)` may turn a hit into no damage (1 blocked, 2 takedown; 0 = as rolled). Needs InstallRollHooks.
using MeleeThac0Fn = int (*)(int32_t attacker, int32_t target);
using MeleeOutcomeFn = uint8_t (*)(int32_t attacker, int32_t target, bool hit, int roll);
void SetMeleeHitFilter(MeleeThac0Fn thac0_add, MeleeOutcomeFn outcome);

// An effect from a feature block, added to the creature's timed list the engine's way
// (CGameEffect::DecodeEffect + CGameSprite::AddEffect, as CMessageAddEffect::Run): saves, immunities and
// feedback are the engine's. `source` = the creature it comes from (-1 none).
struct EffectSpec {
  uint16_t opcode = 0;
  uint32_t param1 = 0, param2 = 0;
  uint8_t timing = 0;        // 0 duration (s), 1 permanent, ...
  uint32_t duration = 0;
  uint32_t save_type = 0;    // feature block bits: 1 spell, 2 breath, 4 death, 8 wand, 16 polymorph
  int32_t save_bonus = 0;
  uint32_t special = 0;
};
bool EffectsAvailable();
bool ApplyEffect(int32_t target, int32_t source, const EffectSpec& e);

// CAIObjectType class (CLASS.IDS) and specifics (SPECIFIC.IDS) of a creature.
bool SpriteClass(int32_t id, uint8_t* cls, uint8_t* specific);
// General state of the derived stats in use (STATE.IDS: 0x1 sleeping, 0x4 panic, 0x8 stunned, 0x20 helpless,
// 0x40 frozen, 0x80 stone, 0x800 dead); -1 when there is no such creature.
int64_t SpriteState(int32_t id);

// Derived stats for the hover card and hit chance.
struct CombatStats {
  int thac0 = 0, ac = 0, hp = 0, max_hp = 0;
};
bool GetCombatStats(int32_t id, CombatStats* out);

// Attacks per round as the engine stores it (derived stat, short encoding: 0..5, 6..10 = 0.5..4.5);
// -1 when there is no such creature.
int NumAttacksStat(int32_t id);
// Makes the engine's next half-attack slot real (true) or skipped (false) for this creature, so the
// 1.5-APR alternation follows the creature's own turn-based rounds (symbols.h kRvaHalfAttack).
void SetHalfAttackReal(int32_t id, bool real);
// Creatures drawn although no party member sees them, as Clairvoyance does (object +0x4C countdown).
void KeepVisible(const std::vector<int32_t>& ids);
// No bored voice lines now: the world screen's bored counter goes back to 0.
void ResetBoredTimer();

// --- action bar ----------------------------------------------------------------------------------
struct BarButton {
  std::string icon;  // BAM resref ("" = empty slot)
  bool disabled = false;
};
// The 12 slots of the engine's action bar as they are now (after CInfButtonArray::SetState).
std::vector<BarButton> ActionBarButtons();
std::string ActionBarHex(int slot, int bytes);  // probe
std::string ActiveFlags(int32_t id);
std::string ScanBarStrings();                    // probe: resref-like strings in the button array
bool SetHpForTest(int32_t id, int hp);          // test probe only (PSTF_SetHp)            // probe: sprite +0x3904..+0x3910 (Spell's target check)

}  // namespace pstf::engine
