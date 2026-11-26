-- scenario: save=000700195 env=PSTF_TBC_AUTOPARTY=1 timeout=500
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S19 Deionarra (1200DEIO, AR1200): attacked -> PlaySequence(5); Wait(3); SetGlobal("GLOBALGame_Over",3);
-- DestroySelf (the game ends: the scenario stops at the variable). The risk: in a fight the turn ends at the
-- Wait and the SetGlobal is lost, or she is nudged into melee. A scripted attack on her does not land (b7), so
-- she is made a combatant and her AttackedBy response is queued as the engine would (one action per EvalAs).
S.steps[75] = function()  -- after S.plot makes her hostile (step 60): the fight is on
	local id = S.caster
	PSTF_EvalAs(id, 'PlaySequence(5)', 1)
	PSTF_EvalAs(id, 'Wait(3)')
	PSTF_EvalAs(id, 'SetGlobal("Game_Over","GLOBAL",3)')
	PSTF_EvalAs(id, 'DestroySelf()')
	S.note('her AttackedBy response queued: ' .. tostring(PSTF_Queue(id)))
end
S.plot{name = 'Deionarra', cre = 'DEIONS', spawn = true, script = '1200DEIO', provoke = false, hostile = true, anyTurns = true,
	expect = {{'Game_Over', 'GLOBAL', 3}}, maxFrames = 4000}
