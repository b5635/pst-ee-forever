-- scenario: save=000700104 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S41 Coaxmetal (AR0501): made hostile, its arm (IGARM: CreateCreatureImpassable) and its spark spells
-- (ForceSpellRESNoFeedback) must join and resolve on their owner's turn; no stuck turn.
S.brawl{name = 'Coaxmetal', provoke = false, hostile = true, rounds = 4, partyHp = true, maxFrames = 9000}
