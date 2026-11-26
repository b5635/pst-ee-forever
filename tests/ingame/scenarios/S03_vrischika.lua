-- scenario: save=000700117 env=PSTF_TBC_AUTOPARTY=1 timeout=800
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S03 Vrischika (0609VRIS, AR0609): LOCALS cd_int_1 = 14: 2209 close / 2405, 2207 far, each behind StartTimer(1,1..4).
S.chainCaster{name = 'Vrischika', pre = {'SetGlobal("cd_int_1","LOCALS",14)'}, near = {260, 40}, casts = 4, turns = 8}
