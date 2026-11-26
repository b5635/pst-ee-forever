-- scenario: save=000700181 env=PSTF_TBC_AUTOPARTY=1 timeout=600
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S43 Pillar of Skulls (1001PILL on PILLAR, AR1001): an immobile creature made hostile. Its turns end (no walk, no
-- stuck), the party can fight it or the stalemate rule ends a fight nobody can win.
S.brawl{name = 'Pillar', provoke = false, hostile = true, hp = 60, maxFrames = 6000}
