-- scenario: save=000700061 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S15 Rat leader (SPELRAT + 0108SRAT, AR0108): ReallyForceSpell chain (2207 x2, 2115, 2107 x2, 2101), then AR0108Rat_Leader_Done = 1.
S.chainCaster{name = 'SPELRAT', cre = 'SPELRAT', spawn = true, script = '0108SRAT', casts = 6, turns = 8, done = {'Rat_Leader_Done', 'AR0108', 1}}
