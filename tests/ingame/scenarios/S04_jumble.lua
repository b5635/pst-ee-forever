-- scenario: save=000700136 env=PSTF_TBC_AUTOPARTY=1 timeout=800
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S04 Jumble (0601JUMB, AR0601): Wait-first step caster (Wait(1); Spell; IncrementGlobal). Counter 1 -> 9, one spell per turn.
S.stepCaster{name = 'JUMBLE', last = 9, maxFrames = 9500}
