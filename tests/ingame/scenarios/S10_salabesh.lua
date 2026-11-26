-- scenario: save=000700130 env=PSTF_TBC_AUTOPARTY=1 timeout=800
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S10 Salabesh (0600SALA, AR0600): Spell; Wait(2); Spell; ... seven spells, one per turn.
S.chainCaster{name = 'Salabesh', casts = 7, turns = 8}
