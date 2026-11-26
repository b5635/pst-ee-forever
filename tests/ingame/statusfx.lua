-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game test for the statusfx module (status-effect visuals instead of the engine's colour pulses).
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\statusfx.lua
--      -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'statusfx: ok|TEST FAIL' -Close -Timeout 240
-- 1. Horror (op 24), stun (op 45), poison (op 25), silence (op 38) on four party members: each state starts
--    its own effect (fear skulls, stun stars + loop, acid, sonic) at the head.
-- 2. A colour pulse (op 9, 30 s) next to the poison runs ~2 s and ends; the poison stays.
-- 3. Blindness, the real spell (SPWI115, saves made hopeless): blind state, blind effect, and its pending
--    S115BLND overlay is pointed at the blank BAM.
-- 4. A Hive thug turned hostile gets magic resistance 100 (op 166); Dak'kon casts Horror at it: the engine
--    says "Spell resisted" and the spell mantle plays on the thug.
-- Shots: pst_forever/logs/statusfx_{states,stun,blind,mantle}.bmp
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local n, party = 0, {}
local fear, stun, pois, sil, blind, mr, dak
local function name(id)
  local _, _, _, _, ref = PSTF_SpritePos(id, 1)
  return ref and Infinity_FetchString(ref) or ''
end
local resists0, blanked0, cut0

local function on(id) local names, parts, bits = PSTF_StatusFxOn(id) return names or '', parts or 0, bits or 0 end
local function bit(v, b) return math.floor(v / b) % 2 == 1 end  -- Lua 5.1: no bit operators
local function has(list, name) return (',' .. list .. ','):find(',' .. name .. ',', 1, true) ~= nil end

function pstfTestTick()
  n = n + 1
  if n == 3 then
    for s = 0, 7 do
      local id = PSTF_PartyId(s)
      if id ~= -1 then party[#party + 1] = id end
    end
    -- Dak'kon casts in step 4: the states go on the others
    local others = {}
    for _, id in ipairs(party) do
      if name(id):find("Dak", 1, true) then dak = id else others[#others + 1] = id end
    end
    if not dak then return fail("no Dak'kon in the party") end
    if #others < 4 then return fail('need 4 party members besides Dakkon, have ' .. #others) end
    pois, fear, stun, sil, blind = others[1], others[2], others[3], others[4], others[5] or others[1]
    local _, _, _, _, _, _, r0, _ = PSTF_StatusFx()
    PSTF_StatusFxApply(fear, 24, 0, 0, 30)
    PSTF_StatusFxApply(stun, 45, 0, 0, 30)
    PSTF_StatusFxApply(pois, 25, 1, 3, 30)
    PSTF_StatusFxApply(pois, 9, 0x00FF0000, 0x001800FF, 30)  -- green pulse, all colours, 30 s
    PSTF_StatusFxApply(sil, 38, 0, 0, 30)
    return
  end
  if n == 20 then
    local f = on(fear)
    local s = on(stun)
    local _, pp = on(pois)
    local _, sp = on(sil)
    PSTF_Note('statusfx: fear "' .. f .. '", stun "' .. s .. '", poison ' .. pp .. ' particles, silence ' .. sp .. ' particles')
    if not has(f, 'fear') then return fail('no fear effect on a horrified creature: "' .. f .. '"') end
    if not has(s, 'stunloop') then return fail('no stun loop on a stunned creature: "' .. s .. '"') end
    if pp == 0 then return fail('no acid particles on a poisoned creature') end
    if sp == 0 then return fail('no sonic particles on a silenced creature') end
    if PSTF_HasEffect(pois, 9, 1) < 0 then return fail('the test pulse did not land') end
    PSTF_LightShot('pst_forever/logs/statusfx_states.bmp')
    local _, _, _, _, c = PSTF_StatusFx()
    cut0 = c
    return
  end
  if n == 26 then PSTF_LightShot('pst_forever/logs/statusfx_stun.bmp') return end
  if n == 30 then
    -- the real Blindness spell, applied to itself with hopeless saves
    PSTF_EvalAs(blind, 'ChangeStat(Myself,SAVEVSSPELL,30,SET)')
    return
  end
  if n == 36 then
    local _, _, _, _, _, b = PSTF_StatusFx()
    blanked0 = b
    PSTF_EvalAs(blind, 'ApplySpellRES("SPWI115",Myself)')
    return
  end
  if n == 120 then
    local q, parts, quads, frames, cut, blanked, resists, starts, last, gl = PSTF_StatusFx()
    local st = PSTF_State(blind)
    local names, bp, bits = on(blind)
    PSTF_Note(string.format('statusfx: blind state %X bits %X particles %d blanked %d (was %d), glows cut %d, quads %d frames %d gl %s',
      st, bits, bp, blanked, blanked0, cut, quads, frames, gl))
    if gl ~= 'ok' then return fail('not drawn: ' .. gl) end
    if quads == 0 then return fail('no quad drawn') end
    if not bit(st, 0x40000) then return fail('Blindness did not blind (state ' .. string.format('%X', st) .. ')') end
    if not bit(bits, 0x40000) then return fail('the watcher did not see the blindness') end
    if blanked <= blanked0 then return fail('the S115BLND overlay was not blanked') end
    if PSTF_HasEffect(pois, 9, 1) >= 0 then return fail('the colour pulse still runs (it should stop after ~2 s)') end
    if PSTF_HasEffect(pois, 25, 1) < 0 then return fail('the poison itself ended') end
    PSTF_LightShot('pst_forever/logs/statusfx_blind.bmp')
    local _, _, _, _, _, _, r = PSTF_StatusFx()
    resists0 = r
    -- a real cast (ApplySpellRES skips the magic resistance roll): Dak'kon casts Horror at a Hive thug
    mr = PSTF_MakeEnemyNearest()
    if mr == -1 then return fail('no creature to cast at') end
    PSTF_StatusFxApply(mr, 166, 100, 0, 120)
    PSTF_EvalAs(dak, 'AddXPObject(Myself,3000000)')
    return
  end
  if n == 130 then PSTF_EvalAs(dak, 'SpellNoDec(NearestEnemyOf(Myself),2213)') return end
  if n >= 132 and n <= 1500 then
    local _, _, _, _, _, _, r = PSTF_StatusFx()
    if r > resists0 then
      local names, parts = on(mr)
      if n % 3 == 0 and parts > 20 then
        PSTF_LightShot('pst_forever/logs/statusfx_mantle.bmp')
        PSTF_Note('statusfx: resisted, mantle with ' .. parts .. ' particles')
        PSTF_Note('statusfx: ok')
        pstfTestTick = nil
      end
      return
    end
    if n == 1500 then return fail('magic resistance 100 resisted nothing (no "Spell resisted")') end
  end
end
