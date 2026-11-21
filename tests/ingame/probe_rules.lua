-- SPDX-License-Identifier: GPL-2.0-or-later
-- Facts before the effect tests: does ApplyDamage work, TNO's regeneration per world second out of
-- combat, Blur's effect type and ticks, Adder's Kiss on a neutral.
local tno = PSTF_PartyId(0)
local t0 = PSTF_WorldTicks()
C:Eval('ApplyDamage(Myself,12,0)', 0)
C:Eval('ForceSpell(Myself,2216)', 1)
local hp0 = nil
local log = {}
local n = 0
function pstfTestTick()
	n = n + 1
	local _, _, hp = PSTF_SpritePos(tno)
	local t = PSTF_WorldTicks()
	if hp0 == nil and n > 3 then hp0 = hp PSTF_Note('after damage hp ' .. hp .. ' at ' .. t) end
	if hp0 and hp ~= log.last then
		PSTF_Note(string.format('tno hp %d at tick %d (+%d)', hp, t, t - t0))
		log.last = hp
	end
	if n == 60 then PSTF_Effects(PSTF_PartyId(1)) PSTF_HasEffect(PSTF_PartyId(1), 65) end
	if n == 400 then PSTF_HasEffect(PSTF_PartyId(1), 65) PSTF_Note('probe done') pstfTestTick = nil end
end
