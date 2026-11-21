// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/lua.h"

#include "core/patch.h"
#include "engine/symbols.h"

namespace pstf::engine {
namespace {
using PushCClosureFn = void(__fastcall*)(LuaState*, LuaCFn, int);
using SetGlobalFn = void(__fastcall*)(LuaState*, const char*);
using PushNumberFn = void(__fastcall*)(LuaState*, double);
using PushStringFn = const char*(__fastcall*)(LuaState*, const char*);
using PushBooleanFn = void(__fastcall*)(LuaState*, int);
using ToNumberXFn = double(__fastcall*)(LuaState*, int, int*);
using GetTopFn = int(__fastcall*)(LuaState*);
using LoadFileXFn = int(__fastcall*)(LuaState*, const char*, const char*);
using PCallKFn = int(__fastcall*)(LuaState*, int, int, int, int, void*);
using SetTopFn = void(__fastcall*)(LuaState*, int);
using ToLStringFn = const char*(__fastcall*)(LuaState*, int, size_t*);

template <class F>
F Fn(uint32_t rva) {
  return reinterpret_cast<F>(Va(rva));
}
}  // namespace

bool LuaApiAvailable() {
  static const int ok = [] {
    const uint32_t need[] = {sym::kRvaLuaPushCClosure, sym::kRvaLuaSetGlobal, sym::kRvaLuaPushNumber,
                             sym::kRvaLuaPushString,   sym::kRvaLuaPushBoolean, sym::kRvaLuaToNumberX,
                             sym::kRvaLuaGetTop,       sym::kRvaLuaPCallK,      sym::kRvaLuaSetTop,
                             sym::kRvaLuaToLString,    sym::kRvaLuaLoadFileX,   sym::kRvaGLuaLoad};
    int found = 0;
    for (const auto& s : sym::kAll) {
      for (uint32_t r : need) {
        if (s.rva != r) continue;
        if (!CheckSymbol(s)) return 0;
        ++found;
      }
    }
    return found == static_cast<int>(sizeof(need) / sizeof(need[0])) ? 1 : 0;
  }();
  return ok != 0;
}

void LuaRegister(LuaState* L, const char* name, LuaCFn fn) {
  Fn<PushCClosureFn>(sym::kRvaLuaPushCClosure)(L, fn, 0);
  Fn<SetGlobalFn>(sym::kRvaLuaSetGlobal)(L, name);
}
void LuaPushNumber(LuaState* L, double v) { Fn<PushNumberFn>(sym::kRvaLuaPushNumber)(L, v); }
void LuaPushString(LuaState* L, const char* s) { Fn<PushStringFn>(sym::kRvaLuaPushString)(L, s); }
void LuaPushBool(LuaState* L, bool v) { Fn<PushBooleanFn>(sym::kRvaLuaPushBoolean)(L, v ? 1 : 0); }
int LuaTop(LuaState* L) { return Fn<GetTopFn>(sym::kRvaLuaGetTop)(L); }
double LuaNumberArg(LuaState* L, int idx, double fallback) {
  if (LuaTop(L) < idx) return fallback;
  int isnum = 0;
  const double v = Fn<ToNumberXFn>(sym::kRvaLuaToNumberX)(L, idx, &isnum);
  return isnum ? v : fallback;
}

std::string LuaDoFile(const char* path) {
  if (!LuaApiAvailable()) return "Lua API differs from the manifest";
  LuaState* L = *reinterpret_cast<LuaState* const volatile*>(sym::kVaGLua);
  if (!L) return "no Lua state";
  int rc = Fn<LoadFileXFn>(sym::kRvaLuaLoadFileX)(L, path, nullptr);
  if (rc == 0) rc = Fn<PCallKFn>(sym::kRvaLuaPCallK)(L, 0, 0, 0, 0, nullptr);
  if (rc == 0) return "";
  const char* msg = Fn<ToLStringFn>(sym::kRvaLuaToLString)(L, -1, nullptr);
  std::string out = "lua error " + std::to_string(rc) + ": " + (msg ? msg : "?");
  Fn<SetTopFn>(sym::kRvaLuaSetTop)(L, -2);
  return out;
}

}  // namespace pstf::engine
