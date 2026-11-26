// SPDX-License-Identifier: GPL-2.0-or-later
// Engine adapter for turn-based combat rules and presentation (symbols.h "round clock for effects",
// "combat presentation and pacing"). Game thread only; callers pass creature ids, never pointers.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
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
// The same objects with an identity while they live (the object's address): status icons give each the
// spell seen on its way (Guardian Mantle is only its attached object, no timed effect). Ended ones (end 0
// or past) are left out.
struct AttachedView {
  uint64_t key = 0;
  int32_t left = 0;
};
std::vector<AttachedView> AttachedObjects(int32_t id);
// Delayed effects that `resolves_now(ticks to onset)` says are part of the spell resolving keep running
// on world time (never shifted) until they fire; ResolvingOnsets counts those still pending.
// resolves_now(ticks to onset, opcode): 0 = round-clocked, 1 = the spell resolving (world time, the
// caster's turn waits for it), 2 = presentation only (world time, nobody waits).
using OnsetFn = int (*)(int32_t left, int opcode);
void SetOnsetRule(OnsetFn resolves_now);
// Optional: sees every delayed effect new on one creature in one pass (they arrived together) and may change
// kinds[i] from what resolves_now gave; source[i] = the effect's parent resref bytes (0 = none).
using OnsetSyncFn = void (*)(const int32_t* left, const int* opcode, const uint64_t* source, int* kinds, int n);
void SetOnsetSync(OnsetSyncFn sync);
int ResolvingOnsets(int32_t id);
void ForgetOnsets();  // fight over
// One round of the creature's own time passes now (at its turn start): timed effects come `ticks`
// closer to their end, natural regeneration accrues `ticks`, persistent effects run `ticks` at once
// (the engine's own catch-up, as CompressTime does), and the effect list is re-evaluated at once.
void StepEffectClock(int32_t id, uint32_t ticks);
// One persistent-effect update with no time passing (HandlePersistantEffects(0)): a persistent effect added during
// a combatant's turn (Prayer's aura) makes its first pulse now, in the turn it was cast, as it would on the next
// real-time tick.
void PulsePersistent(int32_t id);

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
  int opcode = -1;
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

// --- status visuals (statusfx module; symbols.h "Magic resistance", kOffEffRes, kRvaFxRectCall) ----------
// Every effect on the creature (timed and equipped lists): opcode, duration type (4096 running timed,
// 6/7/8 delayed), parent SPL/ITM resref and the effect's own resource (op 369: its BAM), upper case.
struct EffectEntry {
  uint64_t key = 0;  // identity while it lives (compare only)
  int opcode = -1;
  uint32_t type = 0;
  bool equipped = false;
  char parent[9] = {};
  char res[9] = {};
  uint32_t param1 = 0;
};
std::vector<EffectEntry> EffectEntries(int32_t id);
// A colour effect (op 8 glow, 9 pulse, 51 tint, 52 light) of the creature shows nothing from now on: its colour
// becomes the neutral one (black glow, white tint) and the creature's colours are rebuilt on its next effect pass,
// which runs every tick even while the round clock holds it. True when it changed something.
bool QuietColorEffect(int32_t id, uint64_t key);
// A running timed effect (type 4096) of that creature ends on the next world tick, the engine's way (as its
// own expiry). False when it is not on the creature or not running. Presentation effects only (colour glows).
bool ExpireEffectNow(int32_t id, uint64_t key);
// Rewrites the resource of an effect still on the creature (8 chars, "" = none): a pending op 369 plays
// another BAM.
bool SetEffectResource(int32_t id, uint64_t key, const char* res);
// Px above the feet where the engine puts the creature's overhead text (its animation's CalculateFxRect
// reference point + z, as CMessageDisplayText::Run); -1 unknown.
int SpriteTextHeight(int32_t id);
// Called with the target when the engine reports "Spell resisted" (magic resistance; once per spell and
// creature). Needs InstallRollHooks (the CheckSave hook).
using ResistFn = void (*)(int32_t target);
void SetResistListener(ResistFn fn);

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
bool SetSpriteDirection(int32_t id, int dir);
int GameCursorMode();                            // CInfGame +0x78B8 (20 = a hovering missile waits for a target)
bool SetCursorMode(int mode);                     // writes it (a player's Missile of Patience ends itself off 20)
bool MopClickTarget(int32_t caster, int32_t target);  // test probe: a target click for a hovering missile
bool MopUp();                                     // a hovering Missile of Patience is up (g_PST +0x6B4)
// Multi-fire spells (Ice Knife, Force Missiles, Elemental Strike): missiles the caster still has to aim (0: none).
int MultiFireLeft(int32_t caster);
void MultiFireRaw(int32_t caster, int* left, int* spell);
int32_t GameFreezeOwner();  // the projectile freezing the world for its cinematic (g_PST +0x674), -1 none
std::string CinematicProbe(int32_t projectile);  // stage and freeze fields of a PST cinematic spell (log)
uint64_t ProjectileUpdateFn(int32_t projectile);  // its class's AIUpdate VA (which projectile it is; logs)
void AreaListCount(int32_t id, int* front, int* back);  // times it is in its area's front/back object list (probe)  // the two sprite fields as they are (probe)
// one missile at target, as the player's click (MultiFireSpell); spell (2207/2405/2410) first sets the caster's
// readied spell number as a bar cast does (a scripted cast leaves it 0)
bool MultiFire(int32_t caster, int32_t target, int spell = 0);
bool MultiFireDrop(int32_t caster);               // the rest are dropped: count 0, cursor back to normal
// Every Missile of Patience shot (its TransferEffects call): fn(missile) false = the shot does not go.
using MopShotFn = bool (*)(uint64_t missile);
bool InstallMopShotHook(MopShotFn fn);    // CGameSprite::SetDirection: turns there (0..15, 0 south)
inline bool IsAttackSequence(int16_t s) { return s == 0 || s == 8 || s == 11 || s == 12 || s == 13; }
// Lying down: a PST character animation shows its die cycle at `back` frames before the last one (0 = on the
// ground; more = on the way up or down). Returns the cycle's frame count (0: not a PST animation).
int PoseLying(int32_t id, int back);
// The engine's armour hit sound at the creature for a damage type (sym::kHitSoundCrushing = HIT_03<armor>,
// kHitSoundPiercing = HIT_02<armor>), as CGameEffectDamage plays it.
bool PlayArmorHitSound(int32_t id, uint32_t damage_type);
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
  bool crit = false;    // attack: the engine made it a critical hit (Hit's crit out-parameter)
};
// Floating text: every CFloatMessage the engine lists (CGameArea::AddToFloaterList, symbols.h kRvaAddFloater) is
// shown to `fn` first; `fn` returns true to drop it (freed, never drawn; a combat-log copy is the caller's to make).
// Messages raised inside CGameEffectDamage::ApplyEffect carry that effect's damage type bits (param2 >> 16).
struct FloaterInfo {
  int32_t owner = -1;        // the creature it floats over (-1: a world point)
  uint32_t color = 0, type = 0;
  int to_log = 0;            // the engine also puts it in the combat log
  std::string text;
  bool in_damage = false;    // raised by a damage effect
  int32_t damage_target = -1;
  uint32_t damage_bits = 0;  // 0 crushing, 1 acid, 2 cold, 4 electricity, 8 fire, 0x10 piercing, ...
  uint32_t caller_rva = 0;
};
using FloaterFn = bool (*)(const FloaterInfo&);
bool InstallFloaterHook(FloaterFn fn);
bool InstallRollHooks();
// CheckSave posts no "Save vs. <type> : N" line for a save made (symbols.h kRvaSaveFeedbackRead): the save banner
// shows every save instead. "Spell resisted" is untouched. False when the bytes differ (the line stays).
bool SilenceSaveFeedback();
// Test helpers (all-spells suite): remove a spell's effects from a creature (CGameSprite::UnapplySpell; -> effects
// removed, -1 unavailable) and end its PST attached spell objects (shields, mantles) on the next tick.
int UnapplySpell(int32_t id, const char* res);
int EndAttached(int32_t id);
// Test cleanup (PSTF_Unapply): timed effects with no parent resref (an attached spell object's, Power of One's) run
// out on the next tick. -> how many. They outlive EndAttached on their own clocks, as in play.
int ExpireOrphans(int32_t id);
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
// Any derived stat (STATS.IDS number, the active stats: effects included) through the engine's own
// CDerivedStats::GetAtOffset; INT32_MIN when there is no such creature or the symbol is off.
int32_t DerivedStat(int32_t id, int stat);
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
// The engine's whole list behind the bar (CInfButtonArray::pList, after a SetState): entries (0 = none).
int ActionBarListCount();
// Scroll the engine's list to `index` and refill the 12 slots (UpdateButtons); false if not available.
bool ActionBarScroll(int index);
int ActionBarStateId();  // the state the bar is in (+0x1FE4)
std::string ActiveFlags(int32_t id);
std::string ScanBarStrings();                    // probe: resref-like strings in the button array
// Probes: the creature's current action then its queued ones (ACTION.IDS ids, empty = none or symbols off), and its
// script timers {timer id, seconds left} (StartTimer/StartRandomTimer; symbols.h kRvaQueueList, kRvaTimerList).
std::vector<uint16_t> ActionQueue(int32_t id);
std::vector<std::pair<int, int>> ScriptTimers(int32_t id);
// ClearAI(0) that keeps the queued actions (and the running response's markers, so the same script block is not
// queued again): an AI turn that ends inside a real-time sequence (Spell; Wait; IncrementGlobal) goes on with the
// rest of it at its next turn. false = symbols off / not kept (then it was a plain ClearAI).
bool ClearAIKeepQueue(int32_t id);
// Ends the running action only (SetCurrAction(NULL_ACTION)); the queue stays: a script's Wait before its action.
bool EndCurrentAction(int32_t id);
// The queued actions taken out of a creature (its list emptied) and put back later: an attack of opportunity swings
// with an empty queue and the script sequence it had waits on (same CPtrList fields as ClearAIKeepQueue).
struct QueueStash {
  uint64_t list[5] = {};
  uint32_t mark = 0;
  uint16_t mark2 = 0;
  int32_t id = -1;
  bool held = false;
};
bool StashQueue(int32_t id, QueueStash* out);
bool RestoreQueue(QueueStash* st);  // clears what was queued meanwhile first
// 1 = the running action is done and the engine takes the next one on its next update (sprite +0x3E8), 0 not, -1 n/a.
int CurrentActionDone(int32_t id);
bool SetHpForTest(int32_t id, int hp, int max_hp = 0);  // max_hp > 0: base max HP too          // test probe only (PSTF_SetHp)            // probe: sprite +0x3904..+0x3910 (Spell's target check)

}  // namespace pstf::engine
