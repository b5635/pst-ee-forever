-- scenario: save=000700128 env=PSTF_TBC_AUTOPARTY=1 timeout=600
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S39 Modron constructs (1300CONS on CHIGH/CMED/CLOW; the maze saves have none placed: spawned). Its AttackedBy
-- response is "Help(); Enemy(); Attack(LastAttackerOf(Myself))" (no Dialogue in this script): Help brings the
-- others in, the fight ends with no stuck turn.
local before, more = nil, {}
S.steps[32] = function() before = S.ids() end
S.steps[33] = function() S.spawn('CMED', 260, 60) end
S.steps[34] = function() S.spawn('CLOW', 300, 0) end
S.steps[44] = function()
	local party = {}
	for i = 0, 7 do local p = PSTF_PartyId(i) if p and p ~= -1 then party[p] = true end end
	for id in pairs(S.ids()) do
		if not before[id] and not party[id] then more[#more + 1] = id PSTF_EvalAs(id, 'ChangeAIScript("1300CONS",0)', 1) end
	end
	S.note(#more .. ' more constructs')
end
S.brawl{name = 'CHIGH', cre = 'CHIGH', spawn = true, script = '1300CONS', hp = 40, maxFrames = 6000}
