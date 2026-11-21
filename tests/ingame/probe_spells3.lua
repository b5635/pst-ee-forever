-- SPDX-License-Identifier: GPL-2.0-or-later
-- Out of combat: do Cone of Cold and Cloudkill cast at all (party member 2 at Player1 / Player3)?
local P = {PSTF_PartyId(0), PSTF_PartyId(1), PSTF_PartyId(2)}
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h end
local n, last = 0, {}
local steps = {
	{3, "ChangeStat(Myself,34,12,1)"},
	{20, "ForceSpell(Player1,2502)"}, {200, "ForceSpell(Player3,2501)"}, {400, "ForceSpellRES(\"SPWI502\",Player1)"},
	{600, "ForceSpell(Player1,2310)"},
}
function pstfTestTick()
	n = n + 1
	for _, s in ipairs(steps) do if s[1] == n then C:Eval(s[2], 1) PSTF_Note('step ' .. s[2]) end end
	for _, w in ipairs(P) do
		local h = hp(w)
		if last[w] and h ~= last[w] then PSTF_Note(string.format('hp %d %d -> %d at frame %d', w, last[w], h, n)) end
		last[w] = h
	end
	if n == 800 then PSTF_Note('probe done') pstfTestTick = nil end
end
