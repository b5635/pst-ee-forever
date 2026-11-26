-- scenario: save=000700070 env=PSTF_TBC_AUTOPARTY=1 timeout=600
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S18 Soego (1500SOEG, AR1500): attacked in a fight -> poof, recolour, Polymorph(61512), SmallWait, GLOBAL Soego = 4, Enemy, LOCALS cd_int_0 = 1, Attack.
S.maxFrames = 5000
S.plot{name = 'Soego', check = function(id) local a = PSTF_AnimId(id, 1) S.a0 = S.a0 or a return a ~= S.a0, 'animation ' .. S.a0 .. ' -> ' .. a end, expect = {{'Soego', 'GLOBAL', 4}, {'cd_int_0', 'LOCALS', 1, 'self'}}}
