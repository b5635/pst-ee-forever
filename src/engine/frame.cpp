// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/frame.h"

#include <vector>
#include <windows.h>
#include <mmsystem.h>

#include "core/log.h"
#include "core/patch.h"
#include "engine/sprites.h"
#include "engine/sprite_ptr.h"
#include "engine/symbols.h"
#include "modules/smooth/smooth_logic.h"

namespace pstf::engine {
namespace {

template <class T>
T R(uint64_t va) {
  return *reinterpret_cast<const volatile T*>(va);
}

// Snap distances (GemRB: actors 64, projectiles 256 map units). A jump past these is a teleport or an
// area change and draws at its real position instead of sliding there.
constexpr float kSnapSprite = 64.0f;
constexpr float kSnapObject = 256.0f;

using AsyncFn = void(__fastcall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using GameSyncFn = void(__fastcall*)(void*);
using DelayFn = void(__cdecl*)(uint32_t);
using Render3Fn = void(__fastcall*)(void*, void*, void*);
using Render2Fn = void(__fastcall*)(void*, void*);

AsyncFn g_async = nullptr;             // CBaldurChitin::AsynchronousUpdate, the world update
GameSyncFn g_game_sync = nullptr;      // CInfGame::SynchronousUpdate, called inside the draw pass
DelayFn g_delay = nullptr;             // SDL_Delay, EE's frame cap
uint64_t g_sprite_render = 0;          // trampolines: CGameSprite::Render / HealthBar / Markers
uint64_t g_bar_render = 0;
uint64_t g_marker_render = 0;
uint64_t g_temporal_render = 0;        // CGameTemporal::Render: projectiles

smooth::TickClock g_clock;
smooth::TickBudget g_budget;           // accumulated real time, spent one tick_ms per world update
std::vector<smooth::DrawPos> g_rec;    // one per object-array index (id >> 16)
uint64_t g_next_ms = 0;                // nominal start of the next iteration (fixed-timestep pacing)
uint64_t g_frames = 0;
uint64_t g_ticks = 0;
bool g_tick_frame = false;             // this iteration ran the world update
bool g_on = false;
uint32_t g_target_fps = 60;            // requested draw frames per second

uint64_t NowMs() { return GetTickCount64(); }

uint32_t TickMs() {
  int fps = R<int32_t>(sym::kVaEngineFps);
  if (fps < 1 || fps > 240) fps = 40;  // CChitin::InitInstance clamps to 10..90, else 40
  const uint32_t ms = 1000u / static_cast<uint32_t>(fps);
  return ms ? ms : 1u;
}

// The iteration interval, as a whole division of the tick interval: k iterations per tick, k >= 2, so a
// tick always lands on an iteration boundary and the world keeps EE's own rate.
uint32_t FrameMs() {
  const uint32_t tick = TickMs();
  uint32_t k = static_cast<uint32_t>(g_target_fps * tick / 1000u);
  if (k < 2u) k = 2u;
  const uint32_t ms = tick / k;
  return ms ? ms : 1u;
}

smooth::DrawPos* RecOf(int32_t id) {
  const int idx = (id >> 16) & 0x7FFF;
  if (idx >= static_cast<int>(g_rec.size())) g_rec.resize(static_cast<size_t>(idx) + 1);
  return &g_rec[static_cast<size_t>(idx)];
}

// Where this mover is shown this frame, as an integer shift from the position the engine draws at.
bool ShiftOf(int32_t id, const MoveState& m, float snap, int* dx, int* dy) {
  *dx = *dy = 0;
  if (!g_clock.enabled) return false;
  const smooth::DrawShift sh = smooth::InterpOffset(*RecOf(id), m.x, m.y, m.draw_x, m.draw_y,
                                                    g_clock.serial, g_clock.Alpha(NowMs()), snap);
  *dx = sh.dx;
  *dy = sh.dy;
  return *dx != 0 || *dy != 0;
}

// Every mover of the current area remembers where it is, before the world advances.
void Capture() {
  const uint32_t serial = g_clock.serial;
  for (const auto& s : AllSprites()) {
    MoveState m;
    if (SpriteMoveState(s.id, &m)) RecOf(s.id)->Capture(m.x, m.y, serial);
  }
  for (const auto& o : AllObjects()) {
    MoveState m;
    if (ObjectMoveState(o.id, &m)) RecOf(o.id)->Capture(m.x, m.y, serial);
  }
}

void __fastcall AsyncDetour(void* chitin, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
  ++g_frames;
  if (!g_on) {
    g_async(chitin, a, b, c, d, e);
    return;
  }
  const uint64_t now = NowMs();
  if (g_budget.Due(now, TickMs())) {
    g_clock.BeginTick(now, TickMs());
    g_tick_frame = true;
    ++g_ticks;
    Capture();
    g_async(chitin, a, b, c, d, e);
  } else {
    // Draw-only iteration: the world does not advance, and neither does the logic the draw pass runs.
    g_tick_frame = false;
  }
}

void __fastcall GameSyncDetour(void* game) {
  if (g_on && !g_tick_frame) return;
  g_game_sync(game);
}

void __fastcall DelayDetour(uint32_t engine_ms) {
  if (!g_on) {
    g_delay(engine_ms);
    return;
  }
  // Draw pacing on a nominal grid: the grid advances by the frame interval from the previous nominal
  // instant, so a wake-up that overshoots is absorbed by a shorter wait instead of shifting every frame.
  const uint64_t now = NowMs();
  const uint32_t frame_ms = FrameMs();
  if (g_next_ms == 0) g_next_ms = now;
  uint64_t nominal = g_next_ms + frame_ms;
  if (now > nominal) nominal = now + frame_ms;  // fell behind by more than a frame: resync
  g_next_ms = nominal;
  const uint32_t wait = nominal > now ? static_cast<uint32_t>(nominal - now) : 0u;
  g_delay(wait);
}

// Shift this object's drawn position for the duration of one engine draw call; the caller restores it.
// creature = true reads the walk accumulators (exact position), false the integer position.
bool ApplyShift(void* self, bool creature, float snap, int32_t* id, int* ox, int* oy) {
  const uint64_t p = reinterpret_cast<uint64_t>(self);
  if (!g_on || !PlausiblePtr(p)) return false;
  const int32_t i = SpriteIdOf(self);
  *id = i;
  MoveState m;
  if (!(creature ? SpriteMoveState(i, &m) : ObjectMoveState(i, &m))) return false;
  int dx = 0, dy = 0;
  if (!ShiftOf(i, m, snap, &dx, &dy)) return false;
  return creature ? ShiftSpritePos(i, dx, dy, ox, oy) : ShiftObjectPos(i, dx, dy, ox, oy);
}

void RevertShift(int32_t id, bool creature, int x, int y) {
  if (creature)
    RestoreSpritePos(id, x, y);
  else
    RestoreObjectPos(id, x, y);
}

void __fastcall SpriteRender(void* self, void* area, void* vid) {
  int32_t id = -1, ox = 0, oy = 0;
  const bool shifted = ApplyShift(self, true, kSnapSprite, &id, &ox, &oy);
  reinterpret_cast<Render3Fn>(g_sprite_render)(self, area, vid);
  if (shifted) RevertShift(id, true, ox, oy);
}

void __fastcall BarRender(void* self, void* vid) {
  int32_t id = -1, ox = 0, oy = 0;
  const bool shifted = ApplyShift(self, true, kSnapSprite, &id, &ox, &oy);
  reinterpret_cast<Render2Fn>(g_bar_render)(self, vid);
  if (shifted) RevertShift(id, true, ox, oy);
}

void __fastcall MarkerRender(void* self, void* vid) {
  int32_t id = -1, ox = 0, oy = 0;
  const bool shifted = ApplyShift(self, true, kSnapSprite, &id, &ox, &oy);
  reinterpret_cast<Render2Fn>(g_marker_render)(self, vid);
  if (shifted) RevertShift(id, true, ox, oy);
}

void __fastcall TemporalRender(void* self, void* area, void* vid) {
  int32_t id = -1, ox = 0, oy = 0;
  const bool shifted = ApplyShift(self, false, kSnapObject, &id, &ox, &oy);
  reinterpret_cast<Render3Fn>(g_temporal_render)(self, area, vid);
  if (shifted) RevertShift(id, false, ox, oy);
}

const sym::Symbol* Find(uint32_t rva) {
  for (const auto& s : sym::kAll)
    if (s.rva == rva) return &s;
  return nullptr;
}

const sym::Symbol* FindData(uint32_t rva) {
  for (const auto& s : sym::kData)
    if (s.rva == rva) return &s;
  return nullptr;
}

bool Install(uint32_t frame_fps) {
  const uint32_t need[] = {sym::kRvaDelayCall, sym::kRvaGameSyncCall, sym::kRvaFpsRead,
                           sym::kRvaSpriteRender, sym::kRvaHealthBarRender, sym::kRvaMarkersRender,
                           sym::kRvaTemporalRender, sym::kRvaWalkStepXRead, sym::kRvaWalkAccXRead,
                           sym::kRvaWalkAccYAdd, sym::kRvaWalkPosX, sym::kRvaWalkPosY};
  int ok = 0;
  for (uint32_t r : need)
    if (const sym::Symbol* s = Find(r)) ok += CheckSymbol(*s) ? 1 : 0;
  const sym::Symbol* slot = FindData(sym::kRvaSlotChitinAsync);
  if (!slot || ok != static_cast<int>(sizeof(need) / sizeof(need[0])) || !CheckSymbol(*slot)) {
    Log("smooth: hook bytes do not match this Torment.exe (%d/%zu); interpolation off", ok,
        sizeof(need) / sizeof(need[0]));
    return false;
  }

  const uint64_t async = ReplacePointer(*slot, reinterpret_cast<void*>(&AsyncDetour));
  if (async != sym::kVaChitinAsyncUpdate) {
    Log("smooth: tick gate not patched (target %llX); interpolation off",
        static_cast<unsigned long long>(async));
    return false;
  }
  g_async = reinterpret_cast<AsyncFn>(async);

  const uint64_t delay = RedirectCall(*Find(sym::kRvaDelayCall), reinterpret_cast<void*>(&DelayDetour));
  if (delay != sym::kVaSDLDelay) {
    Log("smooth: frame cap not patched (target %llX); interpolation off",
        static_cast<unsigned long long>(delay));
    return false;
  }
  g_delay = reinterpret_cast<DelayFn>(delay);

  const uint64_t sync = RedirectCall(*Find(sym::kRvaGameSyncCall), reinterpret_cast<void*>(&GameSyncDetour));
  if (sync != sym::kVaGameSyncUpdate) {
    Log("smooth: draw-pass logic gate not patched (target %llX); interpolation off",
        static_cast<unsigned long long>(sync));
    return false;
  }
  g_game_sync = reinterpret_cast<GameSyncFn>(sync);

  g_sprite_render = InlineHook(*Find(sym::kRvaSpriteRender), sizeof(sym::kSpriteRenderBytes),
                               reinterpret_cast<void*>(&SpriteRender));
  g_bar_render = InlineHook(*Find(sym::kRvaHealthBarRender), sizeof(sym::kHealthBarRenderBytes),
                            reinterpret_cast<void*>(&BarRender));
  g_marker_render = InlineHook(*Find(sym::kRvaMarkersRender), sizeof(sym::kMarkersRenderBytes),
                               reinterpret_cast<void*>(&MarkerRender));
  g_temporal_render = InlineHook(*Find(sym::kRvaTemporalRender), sizeof(sym::kTemporalRenderBytes),
                                 reinterpret_cast<void*>(&TemporalRender));
  if (!g_sprite_render || !g_bar_render || !g_marker_render || !g_temporal_render) {
    Log("smooth: draw hooks not installed; interpolation off");
    g_async = nullptr;
    g_delay = nullptr;
    g_game_sync = nullptr;
    g_sprite_render = g_bar_render = g_marker_render = g_temporal_render = 0;
    return false;
  }

  g_target_fps = frame_fps > 0 ? frame_fps : 60u;
  g_next_ms = 0;
  // Sub-tick pacing waits in ~10 ms steps, so the system timer must be at 1 ms or every short wait
  // rounds up to the default 15.6 ms resolution and the tick rate (game speed) drifts down.
  timeBeginPeriod(1);
  g_clock.enabled = true;
  g_on = true;
  const uint32_t tick = TickMs();
  Log("smooth: interpolation installed, %u ms iterations (~%u draws/s, EE tick %u ms = %u ticks/s, fps %d)",
      FrameMs(), 1000u / FrameMs(), tick, 1000u / tick, R<int32_t>(sym::kVaEngineFps));
  return true;
}

}  // namespace

bool InstallFrameInterpolation(uint32_t frame_fps) {
  if (g_on) return true;
  return Install(frame_fps);
}

bool InterpActive() { return g_on; }
bool InterpTickFrame() { return !g_on || g_tick_frame; }
uint64_t InterpFrames() { return g_frames; }
uint64_t InterpTicks() { return g_ticks; }
uint32_t InterpTickMs() { return TickMs(); }
float InterpAlpha() { return g_clock.Alpha(NowMs()); }
int InterpEngineFps() { return R<int32_t>(sym::kVaEngineFps); }

}  // namespace pstf::engine
