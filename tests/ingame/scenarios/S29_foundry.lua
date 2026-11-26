-- scenario: save=000700097 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S29 Foundry (0502GENA, AR0502): attack Thildon -> Alarm2 = 1 and the 14400-tick Alarm2_Timer (a global timer: world
-- time, not held); workers Help and run, Godsman guards fight. The fight must end or run on without a stuck turn.
S.brawl{name = 'Thildon', hp = 40, rounds = 4, expect = {{'Alarm2', 'GLOBAL', 1}}}
