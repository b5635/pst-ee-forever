-- scenario: save=000700204 env=PSTF_TBC_AUTOPARTY=1 timeout=1100
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S02 Transcendent One (1204TRNF, AR1204): signature spells 2902, 2506, 2805, 2802 each behind StartTimer(2,30); melee while TimerActive(2). Count turns between big spells (timer 2 runs only in its own turns).
S.chainCaster{name = 'The Transcendent One', script = '1204TRNF', hp = 4000, casts = 3, turns = 16, maxFrames = 14000}
