-- SPDX-License-Identifier: GPL-2.0-or-later
-- Exploration probe: who is here, stats, the action bar in each state.
for s = 0, 5 do
	local id = PSTF_PartyId(s)
	if id ~= -1 then PSTF_Note('party ' .. s .. ' id ' .. id .. ' name ' .. tostring(Infinity_GetPortraitName and Infinity_GetPortraitName(s))) PSTF_Stats(id) end
end
PSTF_Near(2500)
local px, py = PSTF_SpritePos(PSTF_PartyId(0))
PSTF_Note('leader at ' .. px .. ',' .. py)
PSTF_Note('const states: weapons ' .. tostring(const.ACTIONBAR_STATE_WEAPONS) .. ' spells ' .. tostring(const.ACTIONBAR_STATE_SPELLS) .. ' items ' .. tostring(const.ACTIONBAR_STATE_ITEMS) .. ' special ' .. tostring(const.ACTIONBAR_STATE_SPECIAL_ABILITES))
PSTF_Bar()
game:GetButtonArray():SetState(const.ACTIONBAR_STATE_ITEMS)
PSTF_Note('items: occupied ' .. buttonArray:GetNumberOccupiedSlots())
PSTF_Bar()
game:GetButtonArray():SetState(const.ACTIONBAR_STATE_WEAPONS)
PSTF_Note('weapons: occupied ' .. buttonArray:GetNumberOccupiedSlots())
PSTF_Bar()
