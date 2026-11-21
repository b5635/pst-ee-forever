# MODLOG

## 2026-11-21 — examine voice-over (module `voice`)

**Problem.** PST Revoiced (WeiDU, `setup-PST_Revoiced.tp2`) sets TLK sound resrefs (`[VV<strref>]`) for
dialogue and examine lines. Dialogue plays; info-point ("examine", ? cursor, ARE region type 1) text does not.
All 318 info-point strrefs in `D:\pst-voiceover-engine\pst_revoice_ee\infopoints.csv` have the sound set in
`lang/en_US/dialog.tlk`, so the data is fine.

**Engine fact.** `CGameTrigger::OnActionButton` (lin 26A680, win 140218DD0), info-point branch:
`CTlkTable::Fetch(strref, STR_RES&, 0)` fills text **and** `CSound`, then `CScreenWorld::DisplayText` +
`CGameText` show the text, then `~CSound`. No `Play`. There is no script action or Lua hook for this;
region scripts would need patching ~300 ARE entries and would miss areas already stored in saves.

**Route.** Native: the planned single `pst_forever.dll` (all modules) + tiny `version.dll` loader.
- Loader patches the exe entry (RVA 580AA0, 14 bytes checked) for one call, loads `pst_forever.dll`,
  calls `PSTF_Init`, restores the bytes.
- Voice module redirects the one `call Fetch` at 1402196FC (bytes `E8 3F 71 28 00`) through a near trampoline.
  After the real Fetch: if text non-empty and TLK sound present → `SetChannel(0 NARRATIO, no area)`,
  `SetFireForget(1)`, `Play(0)` — the engine's own non-positional voice pattern (`CScreenInventory::SetErrorString`).
  Same strref re-clicked while still playing is skipped (`GetPlayTime` ms).
- Windows `CSound` is a pimpl wrapper (`[rcx+18h]` = CSoundImp). `CSound::Play(int)` = 140494BD0 (port named
  it PickSound; wrong).
- Toggle: `pst_forever\pst_forever.ini` `[voice] examine=1`. No ini = vanilla.

**Verified.** `tools\check.ps1`: build, 6 unit cases, 7/7 manifest symbols vs on-disk exe. In game: log shows
`examine voice installed at 1402196FC` and `examine strref 5059 (4700 ms) played`, `5045 (4360 ms) played`
(Mortuary slab/corpse info points).

**Restore / uninstall.** `tools\deploy.ps1 -Remove` (deletes `version.dll`, `pst_forever.dll`), or set `examine=0`.

**Gotchas.**
1. `python tools/re.py` breaks (shadows stdlib `re`): use `python -P tools/re.py`.
2. Windows Hex-Rays truncates functions IDA marked `__noreturn` (OnActionButton, Play body); read `asm --all`.
3. The other `Fetch` in OnActionButton (1402193DB) is the travel-region message; deliberately not voiced.

**Next.** Ask user to confirm audio by ear; other unvoiced text paths (item descriptions?) if they report any.
