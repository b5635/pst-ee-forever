-- scenario: save=000700106 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S20 Smoldering Corpse bar (AR0402): attack Adahn (0402ADHN: AttackedBy -> Enemy; SC_Hostile = 1; RunAwayFrom). Every
-- patron's "Global(AR0402SC_Hostile,1) & Allegiance(Myself,128) => Enemy()" block turns the bar hostile (Mercykiller
-- patrons, 0402ALIS/BRKS/CAND help). The whole bar joins; the fight runs on without a stuck turn.
S.brawl{name = 'Adahn', hp = 40, rounds = 3, expect = {{'SC_Hostile', 'AR0402', 1}}}
