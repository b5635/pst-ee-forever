-- scenario: save=000700128 env=PSTF_TBC_AUTOPARTY=1 timeout=500
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S07 Modron maze wizard (CBOSS + 1300CWIZ, AR13WZ): 2807 at Player1 (+2), 2504 self, 2301; past 3, WaitRandom then two ReallyForceSpell 2107.
S.stepCaster{name = 'CBOSS', cre = 'CBOSS', script = '1300CWIZ', last = 4}
