-- scenario: save=000700130 env=PSTF_TBC_AUTOPARTY=1 timeout=800
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S10 Salabesh (0600SALA, AR0600): Spell; Wait(2); Spell; ... a priority list; he has 3 of the 7 memorised: one per turn (spent ones fall through), then his RunAwayFrom block.
S.chainCaster{name = 'Salabesh', cre = 'SALABSH', script = '0600SALA', spawn = true, casts = 3, turns = 8}
