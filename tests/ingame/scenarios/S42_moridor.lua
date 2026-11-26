-- scenario: save=000700184 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S42 Fiend from Moridor's Box (AR0800): the boss fight as the save has it. Big HP, AoO, spells: every turn
-- completes, the fight ends (or round 6 begins) with no stuck turn.
S.brawl{name = "Fiend from Moridor's Box", provoke = false, hostile = true, rounds = 6, partyHp = true, maxFrames = 12000}
