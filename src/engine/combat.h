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
// Delayed effects that `resolves_now(ticks to onset)` says are part of the spell resolving keep running
// on world time (never shifted) until they fire; ResolvingOnsets counts those still pending.
using OnsetFn = bool (*)(int32_t left);
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
int PersistentCount(int32_t id);

// Projectiles of creatures for which `held(caster id)` is true do not advance (vtable slot 3 of each
// projectile class met in the area is redirected, after checking it is an engine CGameObject vtable).
using HeldFn = bool (*)(int32_t caster);
void SetProjectileHold(HeldFn held);
void WatchProjectiles();  // call per frame: hooks the vtables of new projectiles

// --- pacing --------------------------------------------------------------------------------------
// Everyone runs: PST character animations through their own SetRunning(1); other animations get
// the run speed on their move scale. `restore` puts the walk speed back.
void ForceRun(int32_t id, bool run, uint8_t run_scale_other);
uint8_t MoveScale(int32_t id);  // current move scale (GetMoveScale)

// Keeps the battle song playing in the area the creature is in (the engine's own counter).
void KeepBattleMusic(int32_t id, int ticks);

// Camera: the view's top left in world pixels (false outside an area) and moving it.
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
};
bool InstallRollHooks();
bool InstallFeedbackLog();  // debug: log every CGameSprite::FeedBack (env PSTF_FEEDBACK_LOG=1)
std::vector<RollEvent> TakeRollEvents();

// Derived stats for the hover card and hit chance.
struct CombatStats {
  int thac0 = 0, ac = 0, hp = 0, max_hp = 0;
};
bool GetCombatStats(int32_t id, CombatStats* out);

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
