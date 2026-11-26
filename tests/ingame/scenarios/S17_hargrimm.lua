-- scenario: save=000700070 env=PSTF_TBC_AUTOPARTY=1 timeout=800
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S17 Hargrimm (1500HARG, AR1500): range- and timer-gated casts 1103 (StartTimer 5), 1204 (15), 1301 (10); HP-gated self heals.
S.chainCaster{name = 'Hargrimm the Bleak', pre = {'Enemy()'}, set = {{'H_Kill_PC', 'AR1500', 1}}, casts = 3, turns = 10}
