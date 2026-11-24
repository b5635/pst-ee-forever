// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/menu.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/patch.h"
#include "engine/symbols.h"

namespace pstf::engine {
namespace {

// Engine CString (MFC layout): one char* to refcounted data.
struct CStr {
  char* p;
};
using CopyFn = CStr*(__fastcall*)(CStr*, const CStr*);
using CtorFn = CStr*(__fastcall*)(CStr*, const char*);

CopyFn g_copy = nullptr;
std::string& Extra() {
  static std::string s;
  return s;
}
std::vector<MenuTransform>& Transforms() {
  static std::vector<MenuTransform> v;
  return v;
}
CStr g_keep{nullptr};  // our reference keeps the combined text alive: tokens point into it

CStr* __fastcall MenuCopy(CStr* out, const CStr* src) {
  if ((Extra().empty() && Transforms().empty()) || !src || !src->p) return g_copy(out, src);
  std::string ui(src->p);
  for (MenuTransform fn : Transforms()) ui = fn(ui);
  const std::string text = ui + "\n" + Extra() + "\n";
  reinterpret_cast<CtorFn>(Va(sym::kRvaCStringCtor))(out, text.c_str());  // refcount 1
  g_copy(&g_keep, out);  // refcount 2: survives the caller's release (old g_keep leaks: tiny, rare)
  Log("menu: UI.MENU %zu bytes + %zu bytes of pst_forever menus", std::strlen(src->p), Extra().size());
  return out;
}

}  // namespace

void AddMenuText(const std::string& menu_text) { Extra() += menu_text + "\n"; }
void AddMenuTransform(MenuTransform fn) { Transforms().push_back(fn); }

bool InstallMenuHook() {
  if (g_copy) return true;
  const sym::Symbol* site = nullptr;
  const sym::Symbol* ctor = nullptr;
  for (const auto& s : sym::kAll) {
    if (s.rva == sym::kRvaMenuCopyCall) site = &s;
    if (s.rva == sym::kRvaCStringCtor) ctor = &s;
  }
  if (!site || !ctor || !CheckSymbol(*ctor)) return false;
  const uint64_t original = RedirectCall(*site, reinterpret_cast<void*>(&MenuCopy));
  if (original != sym::kVaCStringCopy) {
    Log("menu: uiLoadMenu copy site not patched (target %llX)", static_cast<unsigned long long>(original));
    return false;
  }
  g_copy = reinterpret_cast<CopyFn>(original);
  Log("menu: UI.MENU hook installed at %llX", static_cast<unsigned long long>(sym::kImageBase + site->rva));
  return true;
}

}  // namespace pstf::engine
