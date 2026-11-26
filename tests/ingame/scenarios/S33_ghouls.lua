-- scenario: save=000700073 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S33 Drowned Nations ghouls (AR1700; TROCO 1700TRO9 flees at 50 % HP): 11 ghouls against the party. A hurt one runs
-- and is cornered within 1100 px: the fight must still end (or the stalemate rule ends it), no stuck turn.
S.brawl{name = 'Ghoul', provoke = false, maxFrames = 9000}
