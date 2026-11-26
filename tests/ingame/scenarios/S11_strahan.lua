-- scenario: save=000700028 env=PSTF_TBC_AUTOPARTY=1 timeout=800
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S11 Strahan (STRAHAN + 0209NECR in AR0209): 2408 self; Wait; SpellPoint 2213; Wait; 2207; 2201; 2309, one per turn.
S.chainCaster{name = 'Strahan', cre = 'STRAHAN', spawn = true, script = '0209NECR', set = {{'0209_Attack', 'AR0209', 1}}, casts = 5, turns = 6}
