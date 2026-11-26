-- scenario: save=000700202 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S25 Incarnation (1203INC2 on INCAR2, AR1203): AttackedBy -> Enemy; LOCALS cd_int_1 = 1; Polymorph(61521);
-- SetAnimState(36); Help; Attack. The others (1203INC1/3) answer the Help call. Its LOCALS must be set (the
-- response's SetGlobal is instant) and the fight run with its polymorphed animation (log: anim id).
S.brawl{name = 'Incar2', hp = 60, rounds = 3, expect = {{'cd_int_1', 'LOCALS', 1, 'self'}}}
