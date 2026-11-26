-- scenario: save=000700042 env=PSTF_TBC_AUTOPARTY=1 timeout=800
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S09 Shilandra (0306SHIL, AR0306A): Spell x5 then Attack, no waits: a priority list (a spent spell falls through). She knows 3 of them: one per turn, then Attack.
S.chainCaster{name = 'Shilandra', cre = 'SHILAND', script = '0306SHIL', casts = 3, turns = 6}
