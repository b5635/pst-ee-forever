-- scenario: save=000700162 env=PSTF_TBC_AUTOPARTY=1 timeout=1100
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S01 Ravel (0610RAVL, AR0610): step counter LOCALS cd_int_1 1 -> 8 between StartTimer(1,20) self-spells (2203). Watch TIMER 1 across her turns.
S.stepCaster{name = 'Ravel', cre = 'RAVEL', spawn = true, script = '0610RAVL', var = 'cd_int_1', last = 8, maxFrames = 14000}
