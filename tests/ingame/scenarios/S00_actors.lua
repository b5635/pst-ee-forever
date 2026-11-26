-- scenario: save=000700083 env=PSTF_TBC_NOFIGHT=1 timeout=120
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S00 discovery: logs every creature in the save's area (id, script name, shown name, EA, hp, position, action).
-- tools\scenario.ps1 -Id S00 -Save <prefix>
S.steps[3] = function()
	PSTF_Actors(0)
	for sid, ref in string.gmatch(PSTF_ActorRefs(), '(%-?%d+),(%-?%d+);') do
		local r = tonumber(ref)
		S.note(string.format('actor %X name "%s"', tonumber(sid), r and r >= 0 and Infinity_FetchString(r) or '?'))
	end
	S.finish('listed')
end
