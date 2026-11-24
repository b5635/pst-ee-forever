-- SPDX-License-Identifier: GPL-2.0-or-later
-- Probe: what Submerge the Will (SPWI119) and Power of One (SPWI219) put on the Nameless One (effects, status).
local tno
local n = 0
function pstfTestTick()
	n = n + 1
	if n == 2 then tno = PSTF_PartyId(0) C:Eval('AddXPObject(Myself,3000000)', 0) end
	if n == 40 then C:Eval('SpellNoDec(Myself,2119)', 0) PSTF_Note('cast 2119') end
	if n == 200 then C:Eval('SpellNoDec(Myself,2219)', 0) PSTF_Note('cast 2219') end
	if n % 50 == 0 then PSTF_Effects(tno) PSTF_Status(tno) end
	if n == 700 then PSTF_Note('probe_buffs done') pstfTestTick = nil end
end
