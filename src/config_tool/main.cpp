// SPDX-License-Identifier: GPL-2.0-or-later
// pst_forever_config.exe: the options window, deployed next to Torment.exe.
// Edits <game>\pst_forever\pst_forever.ini (schema, defaults and help = the shipped ini, embedded as
// a resource) and the engine options we recommend in Baldur.lua (Disable Cosmetic Attacks).
//
// Command line (no window):
//   --game DIR          game folder (default: the folder of this exe)
//   --upgrade           rewrite the ini in this build's layout, keeping the user's values (deploy)
//   --set S.K=V         set an ini value (repeatable), then save
//   --lua S/K=V         set a Baldur.lua value (repeatable), then save
//   --shot FILE.bmp     open the window, save a picture of it (--section N picks the section), exit
// Exit code 0 = done; 2 = no game folder; 3 = bad argument; 4 = write failed.
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/ini.h"
#include "core/ini_doc.h"

using namespace pstf;

namespace {

constexpr int kResTemplate = 101;
enum : int {
  ID_PF_SECTIONS = 100, ID_PF_LIST, ID_PF_HELP, ID_PF_CHECK, ID_PF_EDIT, ID_PF_DEFLABEL, ID_PF_DEFAULT,
  ID_PF_STATUS, ID_PF_SECTION_DEFAULTS, ID_PF_SAVE, ID_PF_CLOSE
};

// ---- text helpers ----------------------------------------------------------------------------
std::wstring W(std::string_view s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}
std::string U8(std::wstring_view w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}
bool ReadText(const std::wstring& path, std::string* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}
bool WriteFileAtomic(const std::wstring& path, const std::string& text) {
  const std::wstring tmp = path + L".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f) return false;
  }
  return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

// ---- model -----------------------------------------------------------------------------------
struct Row {
  std::string section;  // ini section, or "game" for Baldur.lua rows
  std::string key;      // ini key, or the Baldur.lua key
  std::string lua_section;  // Baldur.lua section ("" = ini row)
  std::string help;
  IniKind kind = IniKind::Text;
  std::string def, value, saved;
  bool available = true;
};

constexpr const char* kGame = "game";
struct LuaOption {
  const char* section;
  const char* key;
  const char* recommended;
  const char* help;
};
const LuaOption kLuaOptions[] = {
    {"Program Options", "Disable Cosmetic Attacks", "1",
     "Recommended: on. The engine's own option (Baldur.lua, also on the game's options screen). Between "
     "its real attacks a fighter plays extra swing animations that roll nothing and deal nothing. With "
     "turn-based combat every swing you see should be one attack roll, and the extra swings also stretch a "
     "turn; turning them off keeps the fight readable. Saved to Baldur.lua: close the game first, it "
     "rewrites the file when it exits."},
};

std::wstring g_game, g_ini, g_lua;
std::string g_tmpl, g_user_ini_text, g_lua_text;
bool g_lua_found = false;
std::vector<Row> g_rows;
std::vector<std::string> g_sections;  // display order; "game" first


bool LoadTemplate() {
  HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(kResTemplate), RT_RCDATA);
  if (!res) return false;
  HGLOBAL h = LoadResource(nullptr, res);
  const char* p = static_cast<const char*>(LockResource(h));
  if (!p) return false;
  g_tmpl.assign(p, SizeofResource(nullptr, res));
  return true;
}

std::wstring ExeDir() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring s(buf, n);
  return s.substr(0, s.find_last_of(L"\\/"));
}

std::wstring BaldurLuaPath() {
  PWSTR docs = nullptr;
  std::wstring p;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)))
    p = std::wstring(docs) + L"\\Planescape Torment - Enhanced Edition\\Baldur.lua";
  CoTaskMemFree(docs);
  return p;
}

bool GameRunning() {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W pe{sizeof(pe)};
  bool found = false;
  for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
    found = _wcsicmp(pe.szExeFile, L"Torment.exe") == 0;
  CloseHandle(snap);
  return found;
}

void LoadModel() {
  g_rows.clear();
  g_sections.clear();
  g_sections.push_back(kGame);
  g_lua_found = ReadText(g_lua, &g_lua_text);
  for (const auto& o : kLuaOptions) {
    Row r;
    r.section = kGame;
    r.key = o.key;
    r.lua_section = o.section;
    r.help = o.help;
    r.kind = IniKind::Bool;
    r.def = o.recommended;
    r.available = g_lua_found;
    r.value = r.saved = g_lua_found ? LuaProfileGet(g_lua_text, o.section, o.key, "0") : "0";
    g_rows.push_back(r);
  }
  g_user_ini_text.clear();
  ReadText(g_ini, &g_user_ini_text);
  const IniMap user = ParseIni(g_user_ini_text);
  for (const IniEntry& e : ParseIniEntries(g_tmpl)) {
    Row r;
    r.section = e.section;
    r.key = e.key;
    r.help = e.help;
    r.kind = e.kind;
    r.def = e.value;
    auto it = user.find(e.Id());
    r.value = r.saved = it != user.end() ? it->second : e.value;
    g_rows.push_back(r);
    if (std::find(g_sections.begin(), g_sections.end(), e.section) == g_sections.end())
      g_sections.push_back(e.section);
  }
}

bool Dirty() {
  for (const Row& r : g_rows)
    if (r.value != r.saved) return true;
  return false;
}

// Writes both files; *err names what failed. Rows must be valid.
bool SaveModel(std::wstring* err) {
  IniMap values = ParseIni(g_user_ini_text);  // keeps keys this build does not know
  bool ini_dirty = g_user_ini_text.empty();
  for (const Row& r : g_rows) {
    if (!r.lua_section.empty()) continue;
    values[r.section + "." + r.key] = r.value;
    ini_dirty |= r.value != r.saved;
  }
  const std::string merged = MergeIni(g_tmpl, values);
  if (ini_dirty || merged != g_user_ini_text) {
    CreateDirectoryW((g_game + L"\\pst_forever").c_str(), nullptr);
    if (!g_user_ini_text.empty()) CopyFileW(g_ini.c_str(), (g_ini + L".bak").c_str(), FALSE);
    if (!WriteFileAtomic(g_ini, merged)) {
      *err = L"Could not write " + g_ini;
      return false;
    }
    g_user_ini_text = merged;
  }
  std::string lua = g_lua_text;
  bool lua_dirty = false;
  for (const Row& r : g_rows) {
    if (r.lua_section.empty() || r.value == r.saved || !r.available) continue;
    lua = LuaProfileSet(lua, r.lua_section, r.key, r.value);
    lua_dirty = true;
  }
  if (lua_dirty) {
    CopyFileW(g_lua.c_str(), (g_lua + L".bak").c_str(), FALSE);
    if (!WriteFileAtomic(g_lua, lua)) {
      *err = L"Could not write " + g_lua;
      return false;
    }
    g_lua_text = lua;
  }
  for (Row& r : g_rows) r.saved = r.value;
  return true;
}

Row* FindRow(std::string_view section, std::string_view key, bool lua) {
  for (Row& r : g_rows) {
    if (lua != !r.lua_section.empty()) continue;
    const std::string s = lua ? r.lua_section : r.section;
    if (_stricmp(s.c_str(), std::string(section).c_str()) == 0 &&
        _stricmp(r.key.c_str(), std::string(key).c_str()) == 0)
      return &r;
  }
  return nullptr;
}

std::string ShowValue(const Row& r, const std::string& v) {
  if (r.kind == IniKind::Bool) return v == "1" ? "on" : v == "0" ? "off" : v;
  return v.empty() ? "(empty)" : v;
}

// ---- window ----------------------------------------------------------------------------------
HWND g_wnd, g_sec, g_list, g_help, g_check, g_edit, g_deflabel, g_default, g_status, g_secdef, g_save, g_close;
HFONT g_font, g_bold;
int g_dpi = 96;
int g_cur = -1;      // row being edited
bool g_filling = false;
int S(int v) { return MulDiv(v, g_dpi, 96); }

std::wstring SectionLabel(const std::string& s) {
  if (s == kGame) return L"Game (Baldur.lua)";
  return W(SectionTitle(s));
}

int CurrentSection() { return static_cast<int>(SendMessageW(g_sec, LB_GETCURSEL, 0, 0)); }

void UpdateTitle() {
  SetWindowTextW(g_wnd, Dirty() ? L"PST Forever options *" : L"PST Forever options");
}

void SetStatus(const std::wstring& s) { SetWindowTextW(g_status, s.c_str()); }

void DefaultStatus() {
  const Row* cos = FindRow("Program Options", "Disable Cosmetic Attacks", true);
  if (!g_lua_found)
    SetStatus(L"Baldur.lua not found (start the game once). Settings: " + g_ini);
  else if (cos && cos->value != "1")
    SetStatus(L"Recommended: turn on Disable Cosmetic Attacks (Game).");
  else if (GameRunning())
    SetStatus(L"The game is running: changes take effect the next time it starts.");
  else
    SetStatus(L"Changes take effect the next time the game starts.");
}

void UpdateListRow(int item) {
  LVITEMW it{};
  it.mask = LVIF_PARAM;
  it.iItem = item;
  ListView_GetItem(g_list, &it);
  const Row& r = g_rows[it.lParam];
  std::wstring v = W(ShowValue(r, r.value));
  if (r.value != r.def) v += L"  \x2022";  // differs from the default
  ListView_SetItemText(g_list, item, 1, v.data());
  std::wstring d = W(ShowValue(r, r.def));
  ListView_SetItemText(g_list, item, 2, d.data());
}

int ListItemOfRow(int row) {
  LVFINDINFOW fi{};
  fi.flags = LVFI_PARAM;
  fi.lParam = row;
  return ListView_FindItem(g_list, -1, &fi);
}

void ShowEditor() {
  g_filling = true;
  const bool any = g_cur >= 0;
  const Row* r = any ? &g_rows[g_cur] : nullptr;
  const bool is_bool = r && r->kind == IniKind::Bool;
  ShowWindow(g_check, is_bool ? SW_SHOW : SW_HIDE);
  ShowWindow(g_edit, any && !is_bool ? SW_SHOW : SW_HIDE);
  ShowWindow(g_deflabel, any ? SW_SHOW : SW_HIDE);
  ShowWindow(g_default, any ? SW_SHOW : SW_HIDE);
  if (r) {
    std::wstring help = W(r->help);
    if (!r->available) help = L"Baldur.lua was not found (start the game once). " + help;
    SetWindowTextW(g_help, help.c_str());
    SetWindowTextW(g_check, W(r->key).c_str());
    SendMessageW(g_check, BM_SETCHECK, r->value == "1" ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(g_edit, W(r->value).c_str());
    std::wstring d = (r->lua_section.empty() ? L"Default: " : L"Recommended: ") + W(ShowValue(*r, r->def));
    SetWindowTextW(g_deflabel, d.c_str());
    EnableWindow(g_check, r->available);
    EnableWindow(g_edit, r->available);
    EnableWindow(g_default, r->available && r->value != r->def);
  } else {
    SetWindowTextW(g_help, L"");
  }
  g_filling = false;
}

void FillList() {
  g_filling = true;
  ListView_DeleteAllItems(g_list);
  const int sec = CurrentSection();
  int item = 0;
  if (sec >= 0 && sec < static_cast<int>(g_sections.size())) {
    for (int i = 0; i < static_cast<int>(g_rows.size()); ++i) {
      if (g_rows[i].section != g_sections[sec]) continue;
      std::wstring key = W(g_rows[i].key);
      LVITEMW it{};
      it.mask = LVIF_TEXT | LVIF_PARAM;
      it.iItem = item;
      it.pszText = key.data();
      it.lParam = i;
      ListView_InsertItem(g_list, &it);
      UpdateListRow(item);
      ++item;
    }
  }
  g_filling = false;
  g_cur = -1;
  if (item > 0) ListView_SetItemState(g_list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
  ShowEditor();
}

void SetValue(int row, const std::string& v) {
  if (row < 0 || !g_rows[row].available) return;
  g_rows[row].value = v;
  int item = ListItemOfRow(row);
  if (item >= 0) UpdateListRow(item);
  EnableWindow(g_default, g_rows[row].value != g_rows[row].def);
  UpdateTitle();
  DefaultStatus();
}

void Layout() {
  RECT rc;
  GetClientRect(g_wnd, &rc);
  const int w = rc.right, h = rc.bottom, m = S(10), btn_h = S(28), btn_w = S(120);
  const int left_w = S(190);
  const int bottom = h - m - btn_h;
  MoveWindow(g_sec, m, m, left_w, bottom - 2 * m, TRUE);
  const int x = m + left_w + m, cw = w - x - m;
  const int editor_h = S(26), help_h = S(110);
  const int list_h = bottom - m - editor_h - m - help_h - 2 * m;
  MoveWindow(g_list, x, m, cw, list_h, TRUE);
  const int lw = cw - GetSystemMetrics(SM_CXVSCROLL) - S(6);
  ListView_SetColumnWidth(g_list, 0, lw * 50 / 100);
  ListView_SetColumnWidth(g_list, 1, lw * 25 / 100);
  ListView_SetColumnWidth(g_list, 2, lw * 25 / 100);
  int y = m + list_h + m;
  MoveWindow(g_help, x, y, cw, help_h, TRUE);
  y += help_h + m;
  MoveWindow(g_check, x, y, cw - S(330), editor_h, TRUE);
  MoveWindow(g_edit, x, y, S(220), editor_h, TRUE);
  MoveWindow(g_deflabel, x + cw - S(320), y + S(4), S(200), editor_h, TRUE);
  MoveWindow(g_default, x + cw - S(110), y - S(1), S(110), editor_h + S(2), TRUE);
  MoveWindow(g_status, m, bottom + S(6), w - 3 * (btn_w + m) - 2 * m, btn_h, TRUE);
  MoveWindow(g_close, w - m - btn_w, bottom, btn_w, btn_h, TRUE);
  MoveWindow(g_save, w - 2 * (m + btn_w), bottom, btn_w, btn_h, TRUE);
  MoveWindow(g_secdef, w - 3 * (m + btn_w), bottom, btn_w, btn_h, TRUE);
}

bool ValidateAll() {
  for (int i = 0; i < static_cast<int>(g_rows.size()); ++i) {
    const Row& r = g_rows[i];
    if (IniValueValid(r.kind, r.value)) continue;
    for (int s = 0; s < static_cast<int>(g_sections.size()); ++s)
      if (g_sections[s] == r.section) SendMessageW(g_sec, LB_SETCURSEL, s, 0);
    FillList();
    int item = ListItemOfRow(i);
    ListView_SetItemState(g_list, item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(g_list, item, FALSE);
    std::wstring msg = W(r.key) + L": \"" + W(r.value) + L"\" is not " +
                       (r.kind == IniKind::Int ? L"a whole number." : L"a valid value (no ';').");
    MessageBoxW(g_wnd, msg.c_str(), L"PST Forever options", MB_ICONWARNING);
    SetFocus(g_edit);
    return false;
  }
  return true;
}

bool DoSave() {
  if (!ValidateAll()) return false;
  bool lua_changed = false;
  for (const Row& r : g_rows) lua_changed |= !r.lua_section.empty() && r.value != r.saved;
  if (lua_changed && GameRunning()) {
    MessageBoxW(g_wnd,
                L"The game is running. It rewrites Baldur.lua when it closes, which would undo the "
                L"Game (Baldur.lua) change. Close the game, then save again.",
                L"PST Forever options", MB_ICONWARNING);
    return false;
  }
  std::wstring err;
  if (!SaveModel(&err)) {
    MessageBoxW(g_wnd, err.c_str(), L"PST Forever options", MB_ICONERROR);
    return false;
  }
  for (int i = 0, n = ListView_GetItemCount(g_list); i < n; ++i) UpdateListRow(i);
  UpdateTitle();
  SetStatus(L"Saved. Changes take effect the next time the game starts.");
  return true;
}

bool ConfirmClose() {
  if (!Dirty()) return true;
  int a = MessageBoxW(g_wnd, L"Save the changes?", L"PST Forever options", MB_YESNOCANCEL | MB_ICONQUESTION);
  if (a == IDCANCEL) return false;
  return a == IDNO || DoSave();
}

HWND Child(const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0) {
  HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g_wnd,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
  SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), FALSE);
  return h;
}

void Create(HWND wnd) {
  g_wnd = wnd;
  HDC dc = GetDC(nullptr);
  g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
  ReleaseDC(nullptr, dc);
  NONCLIENTMETRICSW ncm{sizeof(ncm)};
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
  g_font = CreateFontIndirectW(&ncm.lfMessageFont);
  LOGFONTW bold = ncm.lfMessageFont;
  bold.lfWeight = FW_SEMIBOLD;
  g_bold = CreateFontIndirectW(&bold);

  g_sec = Child(L"LISTBOX", L"", WS_BORDER | WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, ID_PF_SECTIONS);
  g_list = Child(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, ID_PF_LIST, WS_EX_CLIENTEDGE);
  ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
  const wchar_t* cols[] = {L"Setting", L"Value", L"Default"};
  for (int i = 0; i < 3; ++i) {
    LVCOLUMNW c{};
    c.mask = LVCF_TEXT | LVCF_WIDTH;
    c.pszText = const_cast<wchar_t*>(cols[i]);
    c.cx = S(100);
    ListView_InsertColumn(g_list, i, &c);
  }
  g_help = Child(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, ID_PF_HELP, WS_EX_CLIENTEDGE);
  g_check = Child(L"BUTTON", L"", WS_TABSTOP | BS_AUTOCHECKBOX, ID_PF_CHECK);
  SendMessageW(g_check, WM_SETFONT, reinterpret_cast<WPARAM>(g_bold), FALSE);
  g_edit = Child(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, ID_PF_EDIT, WS_EX_CLIENTEDGE);
  g_deflabel = Child(L"STATIC", L"", SS_RIGHT, ID_PF_DEFLABEL);
  g_default = Child(L"BUTTON", L"Use default", WS_TABSTOP, ID_PF_DEFAULT);
  g_status = Child(L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, ID_PF_STATUS);
  g_secdef = Child(L"BUTTON", L"Section defaults", WS_TABSTOP, ID_PF_SECTION_DEFAULTS);
  g_save = Child(L"BUTTON", L"Save", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_PF_SAVE);
  g_close = Child(L"BUTTON", L"Close", WS_TABSTOP, ID_PF_CLOSE);

  for (const std::string& s : g_sections) SendMessageW(g_sec, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(SectionLabel(s).c_str()));
  SendMessageW(g_sec, LB_SETCURSEL, 0, 0);
  Layout();
  FillList();
  UpdateTitle();
  DefaultStatus();
}

LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE:
      Create(wnd);
      return 0;
    case WM_SIZE:
      if (g_list) Layout();
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
      mm->ptMinTrackSize = {S(760), S(480)};
      return 0;
    }
    case WM_NOTIFY: {
      auto* nh = reinterpret_cast<NMHDR*>(lp);
      if (nh->hwndFrom != g_list || g_filling) break;
      if (nh->code == LVN_ITEMCHANGED) {
        auto* nl = reinterpret_cast<NMLISTVIEW*>(lp);
        if ((nl->uNewState & LVIS_SELECTED) && !(nl->uOldState & LVIS_SELECTED)) {
          g_cur = static_cast<int>(nl->lParam);
          ShowEditor();
        }
      } else if (nh->code == NM_DBLCLK && g_cur >= 0 && g_rows[g_cur].kind == IniKind::Bool) {
        SetValue(g_cur, g_rows[g_cur].value == "1" ? "0" : "1");
        ShowEditor();
      } else if (nh->code == NM_DBLCLK && g_cur >= 0) {
        SetFocus(g_edit);
        SendMessageW(g_edit, EM_SETSEL, 0, -1);
      } else if (nh->code == LVN_KEYDOWN && reinterpret_cast<NMLVKEYDOWN*>(lp)->wVKey == VK_SPACE &&
                 g_cur >= 0 && g_rows[g_cur].kind == IniKind::Bool) {
        SetValue(g_cur, g_rows[g_cur].value == "1" ? "0" : "1");
        ShowEditor();
      }
      break;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wp), code = HIWORD(wp);
      if (id == ID_PF_SECTIONS && code == LBN_SELCHANGE) FillList();
      else if (id == ID_PF_CHECK && code == BN_CLICKED && !g_filling)
        SetValue(g_cur, SendMessageW(g_check, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0");
      else if (id == ID_PF_EDIT && code == EN_CHANGE && !g_filling && g_cur >= 0) {
        wchar_t buf[1024];
        GetWindowTextW(g_edit, buf, 1024);
        SetValue(g_cur, U8(buf));
      } else if (id == ID_PF_DEFAULT && g_cur >= 0) {
        SetValue(g_cur, g_rows[g_cur].def);
        ShowEditor();
      } else if (id == ID_PF_SECTION_DEFAULTS) {
        const int sec = CurrentSection();
        for (int i = 0; sec >= 0 && i < static_cast<int>(g_rows.size()); ++i)
          if (g_rows[i].section == g_sections[sec]) SetValue(i, g_rows[i].def);
        ShowEditor();
      } else if (id == ID_PF_SAVE) DoSave();
      else if (id == ID_PF_CLOSE || id == IDCANCEL) PostMessageW(wnd, WM_CLOSE, 0, 0);
      return 0;
    }
    case WM_CTLCOLORSTATIC:
      if (reinterpret_cast<HWND>(lp) == g_help) {  // read-only edit: window background, not grey
        SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
      }
      break;
    case WM_CLOSE:
      if (ConfirmClose()) DestroyWindow(wnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(wnd, msg, wp, lp);
}

HWND MakeWindow(HINSTANCE inst, int show) {
  HICON big = nullptr, small_icon = nullptr;
  ExtractIconExW((g_game + L"\\Torment.exe").c_str(), 0, &big, &small_icon, 1);
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  wc.lpszClassName = L"PstForeverConfig";
  wc.hIcon = big;
  wc.hIconSm = small_icon;
  RegisterClassExW(&wc);
  HDC dc = GetDC(nullptr);
  g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
  ReleaseDC(nullptr, dc);
  HWND w = CreateWindowExW(0, wc.lpszClassName, L"PST Forever options", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT, S(940), S(640), nullptr, nullptr, inst, nullptr);
  ShowWindow(w, show);
  UpdateWindow(w);
  return w;
}

bool SaveShot(HWND w, const std::wstring& path) {
  RECT rc;
  GetWindowRect(w, &rc);
  const int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
  HDC wdc = GetDC(w);
  HDC mem = CreateCompatibleDC(wdc);
  BITMAPINFO bi{};
  bi.bmiHeader = {sizeof(BITMAPINFOHEADER), cw, ch, 1, 32, BI_RGB};
  void* bits = nullptr;
  HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ old = SelectObject(mem, bmp);
  if (!PrintWindow(w, mem, 0)) PrintWindow(w, mem, PW_RENDERFULLCONTENT);
  GdiFlush();
  const DWORD size = static_cast<DWORD>(cw) * 4 * ch;
  BITMAPFILEHEADER fh{0x4D42, sizeof(fh) + sizeof(BITMAPINFOHEADER) + size, 0, 0, sizeof(fh) + sizeof(BITMAPINFOHEADER)};
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
  f.write(reinterpret_cast<const char*>(&bi.bmiHeader), sizeof(BITMAPINFOHEADER));
  f.write(static_cast<const char*>(bits), size);
  SelectObject(mem, old);
  DeleteObject(bmp);
  DeleteDC(mem);
  ReleaseDC(w, wdc);
  return static_cast<bool>(f);
}

void Print(const std::wstring& s) {  // to the console that started us, if any
  static bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
  if (!attached) return;
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  std::string u = U8(s + L"\r\n");
  DWORD n;
  WriteFile(out, u.data(), static_cast<DWORD>(u.size()), &n, nullptr);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::wstring shot;
  int shot_section = 0;
  bool upgrade = false;
  std::vector<std::wstring> sets, lua_sets;
  g_game = ExeDir();
  for (int i = 1; i < argc; ++i) {
    std::wstring a = argv[i];
    auto next = [&]() -> std::wstring { return i + 1 < argc ? argv[++i] : L""; };
    if (a == L"--game") g_game = next();
    else if (a == L"--upgrade") upgrade = true;
    else if (a == L"--set") sets.push_back(next());
    else if (a == L"--lua") lua_sets.push_back(next());
    else if (a == L"--shot") shot = next();
    else if (a == L"--section") shot_section = _wtoi(next().c_str());
    else {
      Print(L"pst_forever_config: unknown argument " + a);
      return 3;
    }
  }
  LocalFree(argv);
  const bool cli = upgrade || !sets.empty() || !lua_sets.empty();
  g_ini = g_game + L"\\pst_forever\\pst_forever.ini";
  g_lua = BaldurLuaPath();
  if (!LoadTemplate()) return 4;
  if (GetFileAttributesW((g_game + L"\\Torment.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
    std::wstring msg = L"Torment.exe is not in " + g_game +
                       L".\nPut pst_forever_config.exe in the Planescape: Torment - Enhanced Edition folder.";
    if (cli) Print(L"pst_forever_config: " + msg);
    else MessageBoxW(nullptr, msg.c_str(), L"PST Forever options", MB_ICONERROR);
    return 2;
  }
  LoadModel();

  if (cli) {
    for (const std::wstring& s : sets) {  // section.key=value
      size_t dot = s.find(L'.'), eq = s.find(L'=');
      Row* r = dot < eq && eq != std::wstring::npos
                   ? FindRow(U8(s.substr(0, dot)), U8(s.substr(dot + 1, eq - dot - 1)), false) : nullptr;
      if (!r || !IniValueValid(r->kind, U8(s.substr(eq + 1)))) {
        Print(L"pst_forever_config: bad --set " + s);
        return 3;
      }
      r->value = U8(s.substr(eq + 1));
    }
    for (const std::wstring& s : lua_sets) {  // Section/Key=value
      size_t sl = s.find(L'/'), eq = s.find(L'=');
      if (sl == std::wstring::npos || eq == std::wstring::npos || eq < sl || !g_lua_found) {
        Print(L"pst_forever_config: bad --lua " + s + (g_lua_found ? L"" : L" (no Baldur.lua)"));
        return 3;
      }
      Row* r = FindRow(U8(s.substr(0, sl)), U8(s.substr(sl + 1, eq - sl - 1)), true);
      if (r) {
        r->value = U8(s.substr(eq + 1));
      } else {  // any other engine option
        g_lua_text = LuaProfileSet(g_lua_text, U8(s.substr(0, sl)), U8(s.substr(sl + 1, eq - sl - 1)), U8(s.substr(eq + 1)));
        if (!WriteFileAtomic(g_lua, g_lua_text)) return 4;
      }
    }
    std::wstring err;
    if (!SaveModel(&err)) {
      Print(L"pst_forever_config: " + err);
      return 4;
    }
    Print(L"pst_forever_config: ok " + g_ini);
    return 0;
  }

  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);
  HWND w = MakeWindow(inst, shot.empty() ? show : SW_SHOWNOACTIVATE);
  if (!shot.empty()) {
    if (shot_section > 0 && shot_section < static_cast<int>(g_sections.size())) {
      SendMessageW(g_sec, LB_SETCURSEL, shot_section, 0);
      FillList();
    }
    RedrawWindow(w, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    for (DWORD t0 = GetTickCount(); GetTickCount() - t0 < 500;) {  // let it paint and compose
      MSG m;
      while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
      Sleep(10);
    }
    bool ok = SaveShot(w, shot);
    DestroyWindow(w);
    return ok ? 0 : 4;
  }
  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0)) {
    if (IsDialogMessageW(w, &m)) continue;
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  return 0;
}
