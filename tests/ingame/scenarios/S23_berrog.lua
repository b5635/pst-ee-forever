-- scenario: save=000700172 env=PSTF_TBC_AUTOPARTY=1 timeout=500
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S23 Berrog (0704BER1, AR0704): sees the party in a fight -> Wait(1), RunToPoint([1913.1859]), Face, ChangeAIScript(0704ber2). The Wait-first sequence runs (skipped Wait, RunToPoint over turns).
S.plot{name = 'Berrog', maxFrames = 4000, check = function(id)
	local x, y = S.pos(id)
	return x and math.abs(x - 1913) + math.abs(y - 1859) < 60, 'at ' .. tostring(x) .. ',' .. tostring(y)
end}
