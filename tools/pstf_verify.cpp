// SPDX-License-Identifier: GPL-2.0-or-later
// L0 static hook verification: every manifest symbol against the on-disk Torment.exe.
// Usage: pstf_verify <path to Torment.exe>. Prints only failures and a summary.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/patch_math.h"
#include "engine/symbols.h"

namespace pstf {
std::string Sha256File(const wchar_t* path);
}

using namespace pstf;

namespace {

struct Image {
  std::vector<uint8_t> d;
  const IMAGE_NT_HEADERS64* nt = nullptr;

  const uint8_t* At(uint32_t rva, size_t n, bool* in_text = nullptr) const {
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
      if (rva >= sec->VirtualAddress && rva + n <= sec->VirtualAddress + sec->Misc.VirtualSize) {
        if (in_text) *in_text = (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        size_t off = sec->PointerToRawData + (rva - sec->VirtualAddress);
        return off + n <= d.size() ? d.data() + off : nullptr;
      }
    }
    return nullptr;
  }
};

int g_fail = 0;
void Fail(const char* what, const char* name) {
  ++g_fail;
  std::printf("FAIL %s: %s\n", name, what);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    std::printf("usage: pstf_verify <Torment.exe>\n");
    return 2;
  }
  std::ifstream f(argv[1], std::ios::binary);
  Image img;
  img.d.assign(std::istreambuf_iterator<char>(f), {});
  if (img.d.size() < 0x1000) {
    std::printf("FAIL cannot read exe\n");
    return 2;
  }
  if (Sha256File(argv[1]) != sym::kExeSha256) Fail("sha256 is not GOG 3.2.2.0", "exe");
  img.nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
      img.d.data() + reinterpret_cast<const IMAGE_DOS_HEADER*>(img.d.data())->e_lfanew);
  if (img.nt->OptionalHeader.ImageBase != sym::kImageBase) Fail("image base", "exe");
  if (img.nt->OptionalHeader.AddressOfEntryPoint != sym::kEntryRva) Fail("entry rva", "exe");
  if (img.nt->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE)
    Fail("ASLR enabled", "exe");

  for (const auto& s : sym::kAll) {
    bool text = false;
    const uint8_t* p = img.At(s.rva, s.len, &text);
    if (!p) {
      Fail("rva outside image", s.name);
      continue;
    }
    if (!text) Fail("not in an executable section", s.name);
    if (!BytesEqual(p, s.bytes, s.len)) Fail("bytes differ", s.name);
  }

  // Cross-checks: the call site really calls Fetch; Play's wrapper reaches the CSoundImp body.
  if (auto* p = img.At(sym::kRvaExamineFetchCall, 5)) {
    auto t = Rel32Target(sym::kImageBase + sym::kRvaExamineFetchCall, p);
    if (!t || *t != sym::kImageBase + sym::kRvaTlkFetch) Fail("call target is not Fetch", "examine");
  }
  if (auto* p = img.At(sym::kRvaPlay + 4, 5)) {
    auto t = Rel32Target(sym::kImageBase + sym::kRvaPlay + 4, p);
    if (!t || *t != 0x140494BF0ull) Fail("wrapper does not jump to 140494BF0", "CSound::Play");
  }

  // RIP-relative operands: target = VA of the next instruction + disp32 at `disp_off`.
  auto rip = [&](uint32_t rva, uint32_t insn_len, uint32_t disp_off, uint64_t want, const char* what) {
    const uint8_t* p = img.At(rva + disp_off, 4);
    int32_t disp = 0;
    if (p) std::memcpy(&disp, p, 4);
    if (!p || sym::kImageBase + rva + insn_len + static_cast<int64_t>(disp) != want)
      Fail("rip-relative target differs", what);
  };
  if (auto* p = img.At(sym::kRvaLuaAutoLoadCall, 5)) {
    auto t = Rel32Target(sym::kImageBase + sym::kRvaLuaAutoLoadCall, p);
    if (!t || *t != sym::kVaLuaLoadFileX) Fail("call target is not luaL_loadfilex", "luaAuto");
  }
  rip(sym::kRvaRoundModeCmp, 7, 2, sym::kVaEngineMode, "ENGINE_MODE");
  rip(sym::kRvaRoundStore, 6, 2, sym::kVaRoundLength, "ROUND_LENGTH");
  rip(sym::kRvaWorldTimeChain, 7, 3, sym::kVaBaldurChitin, "g_pBaldurChitin");
  if (auto* p = img.At(sym::kRvaWorldTimeChain + 23, 5)) {
    auto t = Rel32Target(sym::kImageBase + sym::kRvaWorldTimeChain + 23, p);
    if (!t || *t != sym::kVaTimerUpdateTime) Fail("call target is not UpdateTime", "worldtime");
  }
  auto call_to = [&](uint32_t rva, uint64_t want, const char* what) {
    const uint8_t* p = img.At(rva, 5);
    auto t = p ? Rel32Target(sym::kImageBase + rva, p) : std::nullopt;
    if (!t || *t != want) Fail("call target differs", what);
  };
  call_to(0x226264, sym::kImageBase + sym::kRvaLuaPCallK, "LuaInit -> lua_pcallk");
  call_to(0x2262CB, sym::kImageBase + sym::kRvaLuaSetTop, "LuaInit -> lua_settop");
  call_to(sym::kRvaFrameTickCall, sym::kVaChitinSyncUpdate, "frame tick -> CChitin::SynchronousUpdate");
  rip(sym::kRvaGLuaLoad, 7, 3, sym::kVaGLua, "g_lua");
  rip(sym::kRvaContinueLoadScreen, 7, 3, sym::kVaBaldurChitin, "OnContinueGame chitin");

  for (const auto& s : sym::kData) {
    const uint8_t* p = img.At(s.rva, s.len);
    if (!p || !BytesEqual(p, s.bytes, s.len)) Fail("data bytes differ", s.name);
  }
  call_to(0x226240, sym::kImageBase + sym::kRvaLuaLoadString, "LuaInit -> luaL_loadstring");
  call_to(sym::kRvaWalkCall1, sym::kVaAIUpdateWalk, "AIUpdate -> AIUpdateWalk 1");
  call_to(sym::kRvaWalkCall2, sym::kVaAIUpdateWalk, "AIUpdate -> AIUpdateWalk 2");
  rip(sym::kRvaObjArrayLea, 7, 3, sym::kVaObjArray, "m_pArray");
  rip(sym::kRvaObjMaxCmp, 7, 3, sym::kVaObjMax, "m_maxArrayIndex");
  rip(sym::kRvaObjNextCmp, 7, 3, sym::kVaObjNext, "m_nextObjectId");
  if (sym::kVaSpriteVtable + 35 * 8 != sym::kImageBase + sym::kRvaSlotProcessAI) Fail("slot math", "vtbl");
  {
    uint64_t v = 0;
    std::memcpy(&v, sym::kSlotProcessAIBytes, 8);
    if (v != sym::kImageBase + sym::kRvaProcessAI) Fail("slot does not hold ProcessAI", "vtbl");
  }

  // Turn-based HUD.
  call_to(sym::kRvaMenuCopyCall, sym::kVaCStringCopy, "uiLoadMenu -> CString copy");
  rip(sym::kRvaAnimVtblLea, 7, 3, sym::kVaAnimPstVtable, "PST animation vtable");
  rip(sym::kRvaGridX, 7, 3, sym::kVaGridActualX, "GRID_ACTUALX");
  rip(sym::kRvaGridY, 8, 4, sym::kVaGridActualY, "GRID_ACTUALY");
  rip(sym::kRvaGridSx, 7, 3, sym::kVaGridSquareX, "GRID_SQUARE_SIZEX");
  if (auto* p = img.At(static_cast<uint32_t>(sym::kVaGridSquareX - sym::kImageBase), 2)) {
    if (p[0] != 16 || p[1] != 12) Fail("grid square is not 16x12", "GRID_SQUARE_SIZE");
  }
  if (auto* p = img.At(static_cast<uint32_t>(sym::kVaGridActualX - sym::kImageBase), 6)) {
    if (p[0] != 0x40 || p[1] != 1 || p[4] != 0x40 || p[5] != 1) Fail("grid is not 320x320", "GRID_ACTUAL");
  }

  call_to(sym::kRvaPortraitSelect, sym::kImageBase + sym::kRvaUnselectAll, "portrait click -> UnselectAll");
  call_to(sym::kRvaPortraitSelect + 18, sym::kImageBase + sym::kRvaSelectChar, "portrait click -> SelectCharacter");

  // Lighting (phase 0): composite sites, batch flush, world matrix, program table.
  call_to(sym::kRvaFloatMsgCall, sym::kVaFloatMsgRender, "CGameArea::Render -> CFloatMessage::Render");
  call_to(sym::kRvaTransitionsCall, sym::kVaRenderTransitions, "CGameArea::Render -> RenderTransitions");
  call_to(sym::kRvaFogCall, sym::kVaRenderFog, "CGameArea::Render -> RenderFog");
  call_to(sym::kRvaPostRenderCall, sym::kVaPostRender, "CGameArea::Render -> PostRender");
  call_to(sym::kRvaBeginScaledFlushCall, sym::kImageBase + sym::kRvaDrawFlush, "DrawBeginScaled_GL -> flush");
  call_to(sym::kRvaEndScaledFlushCall, sym::kImageBase + sym::kRvaDrawFlush, "DrawEndScaled_GL -> flush");
  rip(sym::kRvaWorldStStore, 7, 3, sym::kVaWorldSt, "uST slot 1");
  rip(sym::kRvaProgramTableLea, 7, 3, sym::kVaProgramTable, "program table");

  call_to(sym::kRvaAreaRenderCall, sym::kVaAreaRender, "RenderZoomed -> CGameArea::Render");
  call_to(sym::kRvaRenderActionsLine, sym::kImageBase + sym::kRvaDrawLine2d, "RenderActions -> DrawLine2d");
  call_to(sym::kRvaGetCostCall, sym::kImageBase + sym::kRvaGetCost, "SetTarget -> GetCost");
  rip(sym::kRvaImpassableCmp, 6, 2, sym::kVaCostImpassable, "COST_IMPASSABLE");
  if (auto* p = img.At(static_cast<uint32_t>(sym::kVaCostImpassable - sym::kImageBase), 1)) {
    if (p[0] != 0xFF) Fail("COST_IMPASSABLE is not 0xFF", "COST_IMPASSABLE");
  }

  // Effect round clock and combat presentation.
  call_to(sym::kRvaHitFeedbackCall, sym::kVaFeedBack, "Hit -> FeedBack(4)");
  call_to(sym::kRvaSwingHit1, sym::kVaHit, "Swing -> Hit 1");
  call_to(sym::kRvaSwingHit2, sym::kVaHit, "Swing -> Hit 2");
  call_to(sym::kRvaSwingHit3, sym::kVaHit, "Swing -> Hit 3");
  if (sym::kVaAnimPstVtable + sym::kVtblAnimSetRunning != sym::kImageBase + sym::kRvaSlotSetRunning)
    Fail("slot math", "SetRunning vtbl");
  {
    uint64_t v = 0;
    std::memcpy(&v, sym::kSlotSetRunningBytes, 8);
    if (v != sym::kImageBase + sym::kRvaSetRunning) Fail("slot does not hold SetRunning", "SetRunning vtbl");
  }
  call_to(sym::kRvaCenterLeaderJmp, sym::kImageBase + sym::kRvaSetViewPos, "CenterOnGroupLeader -> SetViewPosition");
  // The persistent-effects gate: cmp byte [rcx+7824h] (the world timer's running flag, +4 after the
  // tick at +0x7820) and the jz over the inlined HandlePersistantEffects lands right after it.
  if (auto* p = img.At(sym::kRvaPersistGate, 13)) {
    int32_t rel = 0;
    std::memcpy(&rel, p + 9, 4);
    if (sym::kRvaPersistGate + 13 + rel != 0x425CC3) Fail("jz target differs", "persist gate");
  }

  // Static initialiser of ROUND_LENGTH (.data) is the non-PST default; LoadOptions stores 51.
  if (auto* p = img.At(static_cast<uint32_t>(sym::kVaRoundLength - sym::kImageBase), 4)) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    if (v != 100) Fail("static ROUND_LENGTH is not 100", "ROUND_LENGTH");
  }

  std::printf("verify: %zu symbols, %d failures\n", std::size(sym::kAll) + std::size(sym::kData), g_fail);
  return g_fail ? 1 : 0;
}
