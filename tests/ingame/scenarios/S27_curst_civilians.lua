-- scenario: save=000700165 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S27 Curst civilians (AR0700): attack a citizen; Help turns the nearby citizens, guards and thugs against the party,
-- fleers run. The fight must stay playable: fleers leave once far, fighters fight, no stuck turn.
S.brawl{name = 'Curst Citizen', hp = 40, rounds = 4}
