-- scenario: save=000700110 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S28 Brothel patron (0605FLEE, AR0605): attacked -> Brothel_Alarm = 1, everyone flees, guards come. The fight must end.
-- Vivian has 4 HP: the provoke hit kills her before her own flee turn, so she gets 40. Twenty guards and patrons
-- join; a full fight does not fit a run, so ok = round 3 began (no stuck turn on the way).
S.brawl{name = 'Vivian', hp = 40, rounds = 3, expect = {{'Brothel_Alarm', 'GLOBAL', 1}}}
