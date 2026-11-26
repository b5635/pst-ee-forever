-- scenario: save=000700181 env=PSTF_TBC_AUTOPARTY=1 timeout=600
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S43 Pillar of Skulls (1001PILL on PILLAR, AR1001): an immobile creature made hostile. Its turns end (no walk, no
-- stuck), the party can fight it or the stalemate rule ends a fight nobody can win.
-- Neither walkthrough save has it placed: spawn PILLAR.CRE with the area actor's script.
S.brawl{name = 'PILLAR', cre = 'PILLAR', spawn = true, script = '1001PILL', provoke = false, hostile = true, hp = 60, rounds = 4, maxFrames = 6000}
-- Result: the party needs 31+ to hit it (its vanilla AC): the fight runs on with swings every round (no stalemate).
