-- scenario: save=000700110 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S28 Brothel patron (0605FLEE, AR0605): attacked -> Brothel_Alarm = 1, everyone flees, guards come. The fight must end.
S.brawl{name = 'Vivian', expect = {{'Brothel_Alarm', 'GLOBAL', 1}}}
