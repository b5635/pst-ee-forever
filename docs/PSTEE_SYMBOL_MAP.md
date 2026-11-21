# PST:EE 3.2.2 symbol map

Working map of the engine functions the modules need, for GOG 3.2.2.0 `Torment.exe` x64
(SHA-256 `99ae1586…a3c5`). Snapshot of `resources/re/win/dump/names_win.tsv`, 2026-11-21:
7,525 of 20,059 Windows functions carry a ported Linux name.

`lin` = Linux EA (read the logic there, `python -P tools/re.py code NAME`), `win` = Windows VA (hook target).
`method`/`score` come from `match.py`.

## Tiers

| Tier | Meaning |
|---|---|
| `port-weak` | Matched only through a caller, callee, vtable slot or neighbour, and scored under 4. Likely right, often wrong. |
| `port-strong` | String/offset evidence with score >= 2.5, or any method with score >= 4. |
| `strong` | Confirmed by hand on both sides (same strings, constants and calls). Required before a hook is written (plan §3.2). |
| `confirmed` | Its probe passed in game. Required before a phase is accepted. |

Nothing below is `strong` or `confirmed` yet: every row is a pipeline port. When you confirm one, update its
tier here and add it to `resources/re/win/manual_names.tsv`. `data/Torment-3.2.2.syms` (from
`tools/gen_syms.py`, not written yet) will carry the expected bytes and prologue lengths.

Note: `python tools/re.py` currently crashes because the file name shadows the stdlib `re` module;
run it as `python -P tools/re.py` until that is fixed.

## Core, loader, Lua

| Function (Linux name) | lin | win | method | score | tier |
|---|---|---|---|---|---|
| `main` | 14ED50 | 140144E87 | str1 | 2.79 | port-strong |
| `bootstrapLua(void)` | 156DB0 | 140144420 | str1 | 2.54 | port-strong |
| `uiLoadFunctions(void)` | 580120 | 140458710 | strset | 3.98 | port-strong |
| `CChitin::Update(void)` | 6603A0 | 1404A6350 | str1 | 4.68 | port-strong |
| `CChitin::InitInstance(void)` | 65DD20 | 1404A667D | caller | 3.76 | port-weak |
| `CBaldurChitin::Init(void)` | 157800 | 140140D10 | str1 | 2.54 | port-strong |
| `CBaldurChitin::SynchronousUpdate(void)` | 15BE60 | 140144250 | vtable | 1.00 | port-weak |
| `SDL_GL_SwapWindow` | AACC00 | 1404D53F0 | str1 | 4.17 | port-strong |
| `luaL_newstate` | 6A5F60 | 1400E83F0 | diaphora | 4.75 | port-strong |
| `lua_pcallk` | 6A2B50 | 1400E4EF0 | diaphora | 4.72 | port-strong |
| `luaL_loadstring` | 6A58A0 | 1400E7380 | diaphora | 4.49 | port-strong |
| `lua_pushcclosure` | 6A1A10 | 1400E44C0 | diaphora | 4.66 | port-strong |
| `lua_getglobal` | 6A1B40 | 1400E4610 | callee | 2.50 | port-weak |
| `lua_setglobal` | 6A2240 | 1400E4920 | callee | 2.33 | port-weak |

## Game, party, save

| Function (Linux name) | lin | win | method | score | tier |
|---|---|---|---|---|---|
| `CInfGame::SynchronousUpdate(void)` | 39FE30 | 1402F8C40 | str1 | 3.86 | port-strong |
| `CInfGame::LoadGame(uchar,uchar)` | 397170 | 1402E5690 | str1 | 4.02 | port-strong |
| `CInfGame::SaveGame(uchar,uchar,uchar,uchar)` | 39E400 | 1402EF480 | str1 | 4.32 | port-strong |
| `CInfGame::DestroyGame(uchar,uchar)` | 385110 | 1402D7600 | strset | 5.03 | port-strong |
| `CInfGame::NewGame(uchar,uchar)` | 3934F0 | 1402E9A30 | callee | 2.41 | port-weak |
| `CInfGame::OnPortraitLClick(uint)` | 36E870 | 1402EA180 | neighbour | 5.72 | port-strong |
| `CInfGame::OnPortraitLDblClick(uint)` | 36EBA0 | 1402EA460 | neighbour | 4.23 | port-strong |
| `CInfGame::AddCharacterToParty(int,short)` | 375A70 | 1402D0A70 | callee | 1.54 | port-weak |
| `CInfGame::RemoveCharacterFromParty(int,int &)` | 375DC0 | 1402EB570 | caller | 2.06 | port-weak |
| `CInfGame::SelectCharacter(int,uchar)` | 36A1A0 | 1402F2230 | callee | 1.84 | port-weak |
| `CInfGame::GetCharacterPortraitNum(int)` | 36DAD0 | 1402DB140 | callee | 0.46 | port-weak |
| `CPlanescape::IsCharacterInParty(CAIObjectType const*)` | 5A45B0 | 140256E20 | neighbour | 5.37 | port-strong |

## Actors, AI, actions

| Function (Linux name) | lin | win | method | score | tier |
|---|---|---|---|---|---|
| `CGameSprite::AIUpdate(void)` | 53A510 | 1403B4330 | strset | 4.21 | port-strong |
| `CGameSprite::AIUpdateWalk(void)` | 51FFA0 | 1403B6D20 | caller | 1.76 | port-weak |
| `CGameSprite::DoAction(int)` | 4A26A0 | 1404079D0 | strset | 4.57 | port-strong |
| `CGameSprite::SetCurrAction(CAIAction const&)` | 4A54C0 | 14042B610 | vtable | 1.57 | port-weak |
| `CGameSprite::ProcessPendingTriggers(int)` | 4A24B0 | 140425D30 | strset | 4.49 | port-strong |
| `CGameSprite::SetSequence(short)` | 50A270 | 1403E8800 | callee | 1.73 | port-weak |
| `CGameSprite::SetFacing(short)` | 50A220 | 1403E7080 | callee | 1.00 | port-weak |
| `CGameSprite::DropPath(void)` | 505F80 | 1403C4280 | callee | 1.00 | port-weak |
| `CGameSprite::DropSearchRequest(void)` | 505FB0 | 1403C42B0 | callee | 0.80 | port-weak |
| `CGameAIBase::ExecuteAction(void)` | 197460 | 14016C8E0 | str1 | 4.39 | port-strong |
| `CGameAIBase::ProcessAI(void)` | 191D80 | 140174F10 | vtable | 2.36 | port-weak |
| `CGameAIBase::SetCurrAction(CAIAction const&)` | 183120 | 140175AB0 | vtable | 1.60 | port-weak |
| `CGameAIBase::InsertAction(CAIAction const&)` | 19F5D0 | 14028E600 | vtable | 0.80 | port-weak |
| `CGameAIBase::AddAction(CAIAction const&)` | 19F750 | 140435E60 | vtable | 0.52 | port-weak |
| `CGameAIBase::SetScript(short,CAIScript *)` | 182270 | 140175BB0 | vtable | 1.97 | port-weak |
| `CGameAIBase::ApplyTriggers(void)` | 1825E0 | 1401640C0 | vtable | 0.92 | port-weak |
| `CGameAIBase::ProcessPendingTriggers(int)` | 185650 | 140175290 | caller | 2.47 | port-weak |
| `CGameArea::AIUpdate(void)` | 1B8840 | 14018E0E0 | caller | 2.67 | port-weak |

## Attack, damage, effects, time

| Function (Linux name) | lin | win | method | score | tier |
|---|---|---|---|---|---|
| `CGameSprite::Swing(CGameSprite*)` | 4EAC10 | 1404334E0 | caller | 1.55 | port-weak |
| `CGameSprite::Hit(CGameSprite*,CItem *,int,int &,int)` | 4C29F0 | 1404147B0 | strset | 4.28 | port-strong |
| `CGameSprite::ProcessEffectList(int)` | 4E21F0 | 1404231E0 | callee | 3.99 | port-weak |
| `CGameEffect::DecodeEffect(Item_effect_st *,CPoint const&,int,CPoint const&,int)` | 2012B0 | 1401FE380 | diaphora | 4.55 | port-strong |
| `CGameEffect::OnAdd(CGameSprite *)` | 1EDBA0 | 1402065B0 | vtable | 1.37 | port-weak |
| `CGameEffectList::CheckEffects(void)` | 1F7970 | 1401EC190 | neighbour | 5.40 | port-strong |
| `CGameEffectList::Remove(CGameEffect *,CGameSprite *,int)` | 1F5220 | 140208560 | neighbour | 7.44 | port-strong |
| `CTimerWorld::AddCurrentTime(uint)` | 345AE0 | 1402B2D90 | neighbour | 2.60 | port-weak |
| `CTimerWorld::CheckForTriggerEventPast(void)` | 345FB0 | 1402B2F30 | callee | 2.27 | port-weak |
| `CTimerWorld::StartTime(void)` | 3460F0 | 1402B3360 | callee | 2.42 | port-weak |
| `CTimerWorld::StopTime(void)` | 346150 | 1402B33C0 | strset | 5.42 | port-strong |
| `CGameSprite::Damage(CItem *,CItem *,int,int,CAIObjectType const&,short,short,CGameSprit...` | 4E8A20 | 1404054B1 | str1 | 3.20 | port-strong |

## Movement, pathfinding

| Function (Linux name) | lin | win | method | score | tier |
|---|---|---|---|---|---|
| `CGameArea::CheckWalkable(CPoint const&,CPoint const&,uchar const*,uchar,uchar)` | 1A42B0 | 140190B30 | offsetset | 4.41 | port-strong |
| `CPathSearch::GetPath(short *)` | 2B9470 | 14024E3C0 | neighbour | 6.36 | port-strong |
| `CPathSearch::BuildPathList(CPathNode *,CSearchBitmap *,int)` | 2B8700 | 14024DA50 | neighbour | 5.98 | port-strong |
| `CPathSearch::SmoothPath(int *,int *,uchar,CSearchBitmap *,int)` | 2B8380 | 14024E670 | neighbour | 6.43 | port-strong |
| `CSearchBitmap::AddObject(CPoint const&,uchar,uchar,int)` | 33F6B0 | 1402AD8F0 | callee | 2.46 | port-weak |
| `CPathSearch::FindPath(tagPOINT *,tagPOINT *,short,int,int,CSearchBitmap *,uchar *,int,C...` | 2B8AB0 | 14024DE80 | caller | 2.45 | port-weak |

## Projectiles

| Function (Linux name) | lin | win | method | score | tier |
|---|---|---|---|---|---|
| `CProjectile::DecodeProjectile(ushort,CGameAIBase *,short,CSpell *)` | 2EBCF0 | 14027C4A0 | str1 | 3.54 | port-strong |
| `CProjectile::DeliverEffects(void)` | 2BAB20 | 140282EC0 | vtable | 1.69 | port-weak |
| `CProjectile::OnArrival(void)` | 2BEDA0 | 14028F3E0 | vtable | 1.01 | port-weak |
| `CProjectile::RemoveSelf(void)` | 2B9FE0 | 140290970 | vtable | 1.24 | port-weak |
| `CProjectilePSTMissile::AIUpdate(void)` | 3088B0 | 140263E80 | vtable | 1.00 | port-weak |
| `CProjectilePSTGameAnimatingObject::DoAIUpdate(uchar,int)` | 30E470 | 1402835E0 | vtable | 2.75 | port-weak |
| `CProjectilePSTMissile::Fire(CGameArea *,int,int,CPoint,int,short)` | 2C5FA0 | 140287340 | callee | 1.56 | port-weak |
| `CProjectilePSTGameAnimatingObject::Fire(CGameArea *,int,CPoint,int,CPoint,int,uchar)` | 2C9B70 | 140286C50 | caller | 2.31 | port-weak |
| `CProjectilePSTGameAnimatingObject::Fire(CGameArea *,int,int,CPoint,int,short)` | 2C7BA0 | 140286FC0 | vtable | 1.59 | port-weak |

## Rendering

| Function (Linux name) | lin | win | method | score | tier |
|---|---|---|---|---|---|
| `CResPVR::Demand(void)` | 650B90 | 14047DA20 | neighbour | 10.03 | port-strong |
| `CVidBitmap::BindTexture(short,uint *,int,uchar *,CSize const&,int,uint)` | 689D40 | 1404AD2C0 | neighbour | 6.40 | port-strong |
| `CInfinity::Render(CVidMode *,int)` | 3AB3D0 | 1403062B0 | caller | 6.06 | port-strong |
| `CInfinity::RenderFog(CVidMode *,CVisibilityMap *)` | 3AB1F0 | 1403072A0 | offsetset | 2.86 | port-strong |
| `CVidBitmap::GetLightAffect(int *,int *,int *,uint)` | 68C750 | 14049A200 | neighbour | 4.13 | port-strong |
| `CPlanescape::IsSpriteInDark(CGameSprite *)` | 5A5CE0 | 140257570 | neighbour | 6.46 | port-strong |
| `CGameArea::Render(CVidMode *)` | 1BB560 | 14019E500 | caller | 2.80 | port-weak |
| `CGameSprite::Render(CGameArea *,CVidMode *)` | 562A10 | 1403DD820 | vtable | 1.85 | port-weak |
| `CGameSprite::RenderSpriteEffect(CVidMode *,int,int)` | 50EE70 | 1403E3B90 | strset | 4.06 | port-strong |
| `CVidMosaic::Render(int,int,CRect const&,CRect const&,uint)` | 688DF0 | 1404AE2E0 | callee | 2.15 | port-weak |
| `CInfinity::FXLockPrepForLighting(CRect &,CPoint const&,CPoint const&,CSize const&,uint,...` | 3A5FB0 | 140309990 | callee | 2.86 | port-weak |
| `CGameSprite::RenderSpriteCover(CVidMode *,CVidCell *,uint,uchar,uchar,int,int)` | 50D9F0 | 1403E3950 | token | 5.06 | port-strong |

## Text, sound

| Function (Linux name) | lin | win | method | score | tier |
|---|---|---|---|---|---|
| `CTlkTable::Fetch(uint,STR_RES &,int)` | 673C70 | 1404A0840 | strset | 4.78 | port-strong |
| `CTlkTable::FetchSound(uint,STR_RES &)` | 6738B0 | 1404A0B60 | token | 4.06 | port-strong |
| `CGameSprite::DisplayTextRef(uint,uint,uint,uint,int,int)` | 54B640 | 1403C3860 | token | 3.62 | port-weak |
| `CPlanescape::GetInventoryVOSoundResRef(int)` | 5A48A0 | 1402555E0 | strset | 3.73 | port-strong |
| `CSound::Play(int)` | 66C620 | 1402DD010 | callee | 0.53 | port-weak |
| `CSound::SetResRef(CResRef const&,int,int)` | 669960 | 14034E680 | callee | 0.42 | port-weak |
| `CScreenWorld::DisplayText(CString const&,CString const&,uint,uint,int,int,uint,int,int,...` | 428370 | 140356DC0 | str1 | 5.50 | port-strong |
| `CPlanescape::DisplayDialogText(CString,CString,uint,uint,int,bool)` | 59DF20 | 1402501B6 | strset | 3.43 | port-strong |

## Dialog, input

| Function (Linux name) | lin | win | method | score | tier |
|---|---|---|---|---|---|
| `CScreenWorld::StartDialog(CGameSprite *,CGameSprite *,uchar,uchar,CString const&,int,in...` | 43B7F0 | 140365230 | strset | 4.70 | port-strong |
| `CScreenWorld::EndDialog(uchar,uchar)` | 4236A0 | 140357830 | strset | 5.09 | port-strong |
| `CScreenWorld::OnKeyDown(int)` | 4337F0 | 14035C6B8 | str1 | 4.46 | port-strong |
| `CScreenWorld::OnLButtonDown(CPoint)` | 4231C0 | 1403618D0 | vtable | 1.64 | port-weak |
| `CScreenWorld::OnRButtonDown(CPoint)` | 421C80 | 140362150 | vtable | 2.20 | port-weak |
| `CScreenWorld::TimerSynchronousUpdate(void)` | 42D330 | 140367740 | vtable | 1.95 | port-weak |

## Not ported (Windows EA unknown)

Find each with `re.py xref`/`str` from a ported neighbour; add it to `resources/re/win/manual_names.tsv`.

- `CGameSprite::Attack`
- `CGameSprite::AttackReevaluate`
- `CGameSprite::SetPath`
- `CGameSprite::InitRound`
- `CGameSprite::GetNumAttacks`
- `CGameSprite::Die`
- `CGameAIBase::ClearActions`
- `CTimerWorld::AdvanceCurrentTime`
- `CGameEffectList::HandleList`
- `CPlanescape::SetGameFreeze`
- `CResPVR::TexImage`
