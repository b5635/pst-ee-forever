-- scenario: save=000700088 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S30 Giltspur (0500GILT, AR0500): attacked -> Call_MK = 1 once; Sarge + 4 patrols spawn and run in; the fight ends.
S.brawl{name = 'Giltspur', expect = {{'Call_MK', 'GLOBAL', 1}}}
