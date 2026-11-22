// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/frame.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <windows.h>
#include <mmsystem.h>

#include "core/log.h"
#include "core/patch.h"
#include "engine/render.h"
#include "engine/sprites.h"
#include "engine/sprite_ptr.h"
#include "engine/symbols.h"
#include "modules/smooth/smooth_logic.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

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
using ScrollDestFn = void(__fastcall*)(void*, const int32_t*);

AsyncFn g_async = nullptr;             // CBaldurChitin::AsynchronousUpdate, the world update
GameSyncFn g_game_sync = nullptr;      // CInfGame::SynchronousUpdate, called inside the draw pass
DelayFn g_delay = nullptr;             // SDL_Delay, EE's frame cap
ScrollDestFn g_scroll_dest = nullptr;  // CInfinity::SetScrollDest (the lock camera's call)
uint64_t g_sprite_render = 0;          // trampolines: CGameSprite::Render / HealthBar / Markers
uint64_t g_bar_render = 0;
uint64_t g_marker_render = 0;
uint64_t g_temporal_render = 0;        // CGameTemporal::Render: projectiles

smooth::TickPacer g_pacer;             // accumulated real time, spent one tick_ms per world update
std::vector<smooth::Track> g_tracks;   // one per object-array index (id >> 16)
uint32_t g_serial = 0;                 // world updates since install (0 = none yet)
float g_alpha = 0.0f;                  // this iteration's fraction into the current tick (one per frame)
double g_next_ms = 0.0;                // nominal start of the next iteration (fixed-timestep pacing)
double g_frame_ms = 16.0;              // iteration interval
uint64_t g_frames = 0;
uint64_t g_ticks = 0;
bool g_tick_frame = false;             // this iteration ran the world update
bool g_on = false;
HANDLE g_timer = nullptr;              // high-resolution waitable timer (Windows 10 1803+), else Sleep

// The lock camera's dest as the engine computed it on the latest tick (raw, before SetScrollDest clamps).
struct LockDest {
  uint64_t inf = 0;       // compare only; re-read from the visible area before use
  int32_t raw_x = 0, raw_y = 0;
  int32_t pos_x = 0, pos_y = 0;  // the creature's integer position the raw dest was computed from
  int32_t id = -1;
  uint32_t serial = 0;    // tick it was set on
} g_lock;
bool g_lock_on = false;
uint64_t g_lock_frames = 0;  // frames the view followed the shown position (probe)

// Trace probe (PSTF_SmoothTrace): one row per loop iteration for one creature.
struct TraceRow {
  double t_ms;
  int tick, sx, sy, dx, dy, vx, vy;
  float alpha, ex, ey;
};
int32_t g_trace_id = -1;
int g_trace_left = 0;
std::vector<TraceRow> g_trace;
TraceRow g_row{};
bool g_row_open = false;
TraceResult g_trace_result;

double NowMs() {
  static LARGE_INTEGER f{};
  if (!f.QuadPart) QueryPerformanceFrequency(&f);
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return double(c.QuadPart) * 1000.0 / double(f.QuadPart);
}

int EngineFps() {
  int fps = R<int32_t>(sym::kVaEngineFps);
  if (fps < 1 || fps > 240) fps = 40;  // CChitin::InitInstance clamps to 10..90, else 40
  return fps;
}

// EE's own tick interval as the engine counts it (whole ms), and the exact one we pace at.
uint32_t TickMs() {
  const uint32_t ms = 1000u / static_cast<uint32_t>(EngineFps());
  return ms ? ms : 1u;
}
double TickMsExact() { return 1000.0 / double(EngineFps()); }

uint64_t ViewAreaPtr() {
  const uint64_t g = GameObject();
  if (!g) return 0;
  const uint64_t a = R<uint64_t>(g + sym::kOffGameAreas + 8ull * R<uint8_t>(g + sym::kOffGameVisibleIdx));
  return PlausiblePtr(a) ? a : 0;
}

smooth::Track* TrackOf(int32_t id) {
  const int idx = (id >> 16) & 0x7FFF;
  if (idx >= static_cast<int>(g_tracks.size())) g_tracks.resize(static_cast<size_t>(idx) + 1);
  return &g_tracks[static_cast<size_t>(idx)];
}

// Where this mover is shown this frame (exact, world pixels). False when there is nothing to slide
// (not observed on the latest tick: a fresh object, a new area).
bool ShownAt(int32_t id, float* x, float* y) {
  if (!g_on) return false;
  const smooth::Track* t = TrackOf(id);
  if (!t->Current(id, g_serial)) return false;
  t->At(g_serial, g_alpha, x, y);
  return true;
}

// The shown position as an integer shift from where the engine draws the object.
bool ShiftOf(int32_t id, const MoveState& m, int* dx, int* dy) {
  *dx = *dy = 0;
  float x = 0.0f, y = 0.0f;
  if (!ShownAt(id, &x, &y)) return false;
  *dx = smooth::Pixel(x) - m.draw_x;
  *dy = smooth::Pixel(y) - m.draw_y;
  return *dx != 0 || *dy != 0;
}

// Every mover of the current area reports where the world update left it.
void Observe() {
  const uint32_t serial = g_serial;
  for (const auto& s : AllSprites()) {
    MoveState m;
    if (SpriteMoveState(s.id, &m)) TrackOf(s.id)->Observe(s.id, m.x, m.y, serial, kSnapSprite);
  }
  for (const auto& o : AllObjects()) {
    MoveState m;
    if (ObjectMoveState(o.id, &m)) TrackOf(o.id)->Observe(o.id, m.x, m.y, serial, kSnapObject);
  }
}

// --- trace probe ---------------------------------------------------------------------------------
void TraceView(int* vx, int* vy) {
  *vx = *vy = -1;
  const uint64_t a = ViewAreaPtr();
  if (!a) return;
  *vx = R<int32_t>(a + sym::kOffAreaInfinity + sym::kOffInfView);
  *vy = R<int32_t>(a + sym::kOffAreaInfinity + sym::kOffInfView + 4);
}

void TraceClose() {
  if (!g_row_open) return;
  g_row_open = false;
  TraceView(&g_row.vx, &g_row.vy);
  g_trace.push_back(g_row);
  if (--g_trace_left > 0) return;
  Log("smooth trace: %zu rows for %X (t_ms tick alpha engine exact shift view)", g_trace.size(),
      static_cast<unsigned>(g_trace_id));
  for (const auto& r : g_trace)
    Log("smooth trace: %.2f %d %.2f %d,%d %.2f,%.2f %d,%d %d,%d", r.t_ms, r.tick, r.alpha, r.sx, r.sy, r.ex, r.ey,
        r.dx, r.dy, r.vx, r.vy);
  Log("smooth trace: end");
  // Summary for PSTF_SmoothTraceResult: frames drawn, frames inside the walk where the shown position
  // did not move, and the largest wobble of the creature against the view (screen = shown - view).
  TraceResult res;
  std::vector<const TraceRow*> drawn;
  for (const auto& r : g_trace)
    if (r.dx != -9999) drawn.push_back(&r);
  res.frames = static_cast<int>(drawn.size());
  int first = -1, last = -1;
  for (size_t i = 1; i < drawn.size(); ++i) {
    const bool moved = drawn[i]->sx + drawn[i]->dx != drawn[i - 1]->sx + drawn[i - 1]->dx ||
                       drawn[i]->sy + drawn[i]->dy != drawn[i - 1]->sy + drawn[i - 1]->dy;
    if (moved) {
      if (first < 0) first = static_cast<int>(i);
      last = static_cast<int>(i);
    }
  }
  double moved_at = first > 0 ? drawn[static_cast<size_t>(first)]->t_ms : 0.0;
  for (int i = first; first > 0 && i <= last; ++i) {
    const auto* a = drawn[static_cast<size_t>(i) - 1];
    const auto* b = drawn[static_cast<size_t>(i)];
    if (a->sx + a->dx == b->sx + b->dx && a->sy + a->dy == b->sy + b->dy) {
      ++res.still;
    } else {
      // How long the shown position stood still before this frame moved it.
      res.hold_ms = std::max(res.hold_ms, static_cast<int>(b->t_ms - moved_at + 0.5));
      moved_at = b->t_ms;
    }
    ++res.walk;
  }
  if (!drawn.empty()) {
    const int x0 = drawn[0]->sx + drawn[0]->dx - drawn[0]->vx, y0 = drawn[0]->sy + drawn[0]->dy - drawn[0]->vy;
    for (const auto* r : drawn) {
      const int ex = std::abs(r->sx + r->dx - r->vx - x0), ey = std::abs(r->sy + r->dy - r->vy - y0);
      res.wobble = std::max(res.wobble, std::max(ex, ey));
    }
  }
  res.done = true;
  g_trace_result = res;
  Log("smooth trace: summary frames %d, walk frames %d, standing still %d, longest hold %d ms, wobble %d px",
      res.frames, res.walk, res.still, res.hold_ms, res.wobble);
  g_trace.clear();
  g_trace_id = -1;
}

void TraceOpen(bool tick) {
  if (g_trace_id == -1 || g_trace_left <= 0) return;
  g_row = TraceRow{};
  g_row.t_ms = NowMs();
  g_row.tick = tick ? 1 : 0;
  g_row.alpha = g_alpha;
  g_row.dx = g_row.dy = -9999;
  g_row_open = true;
}

// --- loop ----------------------------------------------------------------------------------------
void __fastcall AsyncDetour(void* chitin, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
  ++g_frames;
  TraceClose();
  if (!g_on) {
    g_async(chitin, a, b, c, d, e);
    return;
  }
  const double tick = TickMsExact();
  if (g_pacer.Due(NowMs(), tick)) {
    g_tick_frame = true;
    ++g_ticks;
    g_async(chitin, a, b, c, d, e);
    if (++g_serial == 0) ++g_serial;  // 0 stays reserved for "never observed"
    Observe();
  } else {
    // Draw-only iteration: the world does not advance, and neither does the logic the draw pass runs.
    g_tick_frame = false;
  }
  // One alpha for the whole frame, so every mover (and the camera) is shown at the same instant.
  g_alpha = g_pacer.Alpha(tick);
  TraceOpen(g_tick_frame);
}

void __fastcall GameSyncDetour(void* game) {
  if (g_on && !g_tick_frame) return;
  g_game_sync(game);
}

// Waits until the given instant: a high-resolution timer when there is one (no busy loop), else
// SDL_Delay in whole ms (timeBeginPeriod(1) is set).
void WaitUntil(double target) {
  const double now = NowMs();
  if (target <= now) return;
  const double wait = target - now;
  if (g_timer) {
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>(wait * 10000.0);  // relative, 100 ns units
    if (SetWaitableTimerEx(g_timer, &due, 0, nullptr, nullptr, nullptr, 0)) {
      WaitForSingleObject(g_timer, 1000);
      return;
    }
  }
  g_delay(static_cast<uint32_t>(wait));
}

void __fastcall DelayDetour(uint32_t engine_ms) {
  if (!g_on) {
    g_delay(engine_ms);
    return;
  }
  // Pacing on a nominal grid: the grid advances by the frame interval from the previous nominal
  // instant, so a wake-up that overshoots is absorbed by a shorter wait instead of shifting every frame.
  const double now = NowMs();
  // Never slower than the tick rate (EE's fps can change at runtime: Baldur.lua, the options screen),
  // or the world could not take its ticks one per iteration and would fall behind.
  const double frame = g_frame_ms < TickMsExact() ? g_frame_ms : TickMsExact();
  if (g_next_ms == 0.0) g_next_ms = now;
  double nominal = g_next_ms + frame;
  if (now > nominal + frame) nominal = now;  // fell behind by more than a frame: resync
  g_next_ms = nominal;
  WaitUntil(nominal);
}

// --- draw shifts ---------------------------------------------------------------------------------
// Shift this object's drawn position for the duration of one engine draw call; the caller restores it.
bool ApplyShift(void* self, bool creature, int32_t* id, int* ox, int* oy) {
  const uint64_t p = reinterpret_cast<uint64_t>(self);
  if (!g_on || !PlausiblePtr(p)) return false;
  const int32_t i = SpriteIdOf(self);
  *id = i;
  MoveState m;
  if (!(creature ? SpriteMoveState(i, &m) : ObjectMoveState(i, &m))) return false;
  int dx = 0, dy = 0;
  const bool moved = ShiftOf(i, m, &dx, &dy);
  if (creature && g_row_open && i == g_trace_id && g_row.dx == -9999) {
    g_row.sx = m.draw_x;
    g_row.sy = m.draw_y;
    g_row.ex = m.x;
    g_row.ey = m.y;
    g_row.dx = dx;
    g_row.dy = dy;
  }
  if (!moved) return false;
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
  const bool shifted = ApplyShift(self, true, &id, &ox, &oy);
  reinterpret_cast<Render3Fn>(g_sprite_render)(self, area, vid);
  if (shifted) RevertShift(id, true, ox, oy);
}

void __fastcall BarRender(void* self, void* vid) {
  int32_t id = -1, ox = 0, oy = 0;
  const bool shifted = ApplyShift(self, true, &id, &ox, &oy);
  reinterpret_cast<Render2Fn>(g_bar_render)(self, vid);
  if (shifted) RevertShift(id, true, ox, oy);
}

void __fastcall MarkerRender(void* self, void* vid) {
  int32_t id = -1, ox = 0, oy = 0;
  const bool shifted = ApplyShift(self, true, &id, &ox, &oy);
  reinterpret_cast<Render2Fn>(g_marker_render)(self, vid);
  if (shifted) RevertShift(id, true, ox, oy);
}

void __fastcall TemporalRender(void* self, void* area, void* vid) {
  int32_t id = -1, ox = 0, oy = 0;
  const bool shifted = ApplyShift(self, false, &id, &ox, &oy);
  reinterpret_cast<Render3Fn>(g_temporal_render)(self, area, vid);
  if (shifted) RevertShift(id, false, ox, oy);
}

// --- lock camera ---------------------------------------------------------------------------------
// The engine's lock camera, once per tick: remember the dest it asked for and the position it used.
void __fastcall LockScrollDest(void* inf, const int32_t* pt) {
  g_scroll_dest(inf, pt);
  if (!g_on || !pt) return;
  const uint64_t c = R<uint64_t>(sym::kVaBaldurChitin);
  const uint64_t w = c ? R<uint64_t>(c + sym::kOffChitinWorldScreen) : 0;
  const int32_t id = PlausiblePtr(w) ? R<int32_t>(w + sym::kOffWorldScrollLock) : -1;
  MoveState m;
  if (id == -1 || !SpriteMoveState(id, &m)) return;
  g_lock.inf = reinterpret_cast<uint64_t>(inf);
  g_lock.raw_x = pt[0];
  g_lock.raw_y = pt[1];
  g_lock.pos_x = m.draw_x;
  g_lock.pos_y = m.draw_y;
  g_lock.id = id;
  g_lock.serial = g_serial + 1;  // the world update in progress; g_serial counts it once it returns
}

// Before each world draw: the locked view follows where the creature is SHOWN, by the same shift the
// draw hook gives it, so the creature stays still on screen and the world glides under it. Only while
// the engine itself set the dest on the latest tick (lock on, same area, world screen): otherwise the
// view is the engine's.
void LockFollow() {
  if (!g_on || !g_lock_on || g_lock.serial != g_serial || g_lock.id == -1) return;
  const uint64_t area = ViewAreaPtr();
  if (!area || area + sym::kOffAreaInfinity != g_lock.inf) return;
  int x = 0, y = 0;
  if (!InterpDrawPos(g_lock.id, &x, &y)) return;
  // raw was computed from pos; the shown pixel differs from it by the slide (and by any step the
  // creature took later in the same world update).
  const int32_t pt[2] = {g_lock.raw_x + (x - g_lock.pos_x), g_lock.raw_y + (y - g_lock.pos_y)};
  g_scroll_dest(reinterpret_cast<void*>(g_lock.inf), pt);
  *reinterpret_cast<volatile int16_t*>(area + sym::kOffAreaScrollSpeed) = 0;
  ++g_lock_frames;
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

uint32_t DisplayHz() {
  DEVMODEW dm{};
  dm.dmSize = sizeof(dm);
  if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
    return dm.dmDisplayFrequency;
  return 60;
}

// The lock follow: optional. Without it the lock camera is the engine's (it jumps per tick).
void InstallLockFollow() {
  const uint32_t need[] = {sym::kRvaLockScrollDestCall, sym::kRvaLockIdRead, sym::kRvaLockSpeedStore,
                           sym::kRvaAreaDestRead, sym::kRvaAreaSpeedRead};
  for (uint32_t r : need) {
    const sym::Symbol* s = Find(r);
    if (!s || !CheckSymbol(*s)) {
      Log("smooth: lock camera bytes differ; the lock camera keeps the engine's tick-rate steps");
      return;
    }
  }
  if (!InstallWorldPreDraw(&LockFollow)) {
    Log("smooth: no pre-draw hook; the lock camera keeps the engine's tick-rate steps");
    return;
  }
  const uint64_t dest = RedirectCall(*Find(sym::kRvaLockScrollDestCall), reinterpret_cast<void*>(&LockScrollDest));
  if (dest != sym::kVaSetScrollDest) {
    Log("smooth: lock camera call not patched (target %llX)", static_cast<unsigned long long>(dest));
    return;
  }
  g_scroll_dest = reinterpret_cast<ScrollDestFn>(dest);
  g_lock_on = true;
}

bool Install(uint32_t frame_fps, bool lock_follow) {
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
    // The loop hooks are in, so keep them: with g_on false they pass straight through (vanilla).
    Log("smooth: draw hooks not installed; interpolation off");
    return false;
  }

  // frame_rate 0 = the display's refresh rate; never below the tick rate (one iteration per tick at
  // least, or the world would fall behind).
  uint32_t hz = frame_fps ? frame_fps : DisplayHz();
  if (hz > 240) hz = 240;
  if (hz < 10) hz = 10;
  const int fps = EngineFps();
  g_frame_ms = 1000.0 / double(hz);
  g_next_ms = 0.0;
  // A high-resolution timer waits to within ~0.5 ms without a busy loop; else short SDL_Delay waits
  // need the 1 ms system timer, or every wait rounds up to 15.6 ms and the tick rate drifts down.
  g_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  timeBeginPeriod(1);
  g_on = true;
  if (lock_follow) InstallLockFollow();
  Log("smooth: interpolation installed, %.2f ms iterations (~%u draws/s), EE tick %.2f ms = %d ticks/s, "
      "%s timer, lock camera %s",
      g_frame_ms, hz, TickMsExact(), fps, g_timer ? "high-resolution" : "sleep",
      g_lock_on ? "follows the shown position" : "engine");
  return true;
}

}  // namespace

bool InstallFrameInterpolation(uint32_t frame_fps, bool lock_follow) {
  if (g_on) return true;
  return Install(frame_fps, lock_follow);
}

bool InterpDrawPos(int32_t id, int* x, int* y) {
  if (!x || !y) return false;
  MoveState m;
  if (!SpriteMoveState(id, &m)) return false;
  int dx = 0, dy = 0;
  ShiftOf(id, m, &dx, &dy);
  *x = m.draw_x + dx;
  *y = m.draw_y + dy;
  return true;
}

TraceResult InterpTraceResult() { return g_trace_result; }

void InterpTrace(int32_t id, int frames) {
  g_trace_result = TraceResult{};
  g_trace.clear();
  g_row_open = false;
  g_trace_id = id;
  g_trace_left = frames;
}

bool LockCamera(int32_t id) {
  const uint64_t c = R<uint64_t>(sym::kVaBaldurChitin);
  const uint64_t w = c ? R<uint64_t>(c + sym::kOffChitinWorldScreen) : 0;
  if (!PlausiblePtr(w)) return false;
  *reinterpret_cast<volatile int32_t*>(w + sym::kOffWorldScrollLock) = id;
  return true;
}

bool InterpActive() { return g_on; }
bool InterpTickFrame() { return !g_on || g_tick_frame; }
uint64_t InterpFrames() { return g_frames; }
uint64_t InterpTicks() { return g_ticks; }
uint64_t InterpLockFrames() { return g_lock_frames; }
uint32_t InterpTickMs() { return TickMs(); }
float InterpAlpha() { return g_alpha; }
int InterpEngineFps() { return R<int32_t>(sym::kVaEngineFps); }

}  // namespace pstf::engine
