-- scenario: save=000700106 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S20 Smoldering Corpse bar (AR0402): attack Adahn (0402ADHN: AttackedBy -> Enemy; SC_Hostile = 1; RunAwayFrom). Every
-- patron's "Global(AR0402SC_Hostile,1) & Allegiance(Myself,128) => Enemy()" block turns the bar hostile (Mercykiller
-- patrons, 0402ALIS/BRKS/CAND help). The whole bar joins; the fight runs on without a stuck turn.
-- b14: the protagonist's attack on Adahn opens a dialogue (no hit, no AttackedBy): his response is played instead.
S.brawl{name = 'Adahn', provoke = false, hostile = true, hp = 40, rounds = 3, set = {{'SC_Hostile', 'AR0402', 1}},
	expect = {{'SC_Hostile', 'AR0402', 1}}}
