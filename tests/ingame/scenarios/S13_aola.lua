-- scenario: save=000700046 env=PSTF_TBC_AUTOPARTY=1 timeout=800
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S13 Aola (AOLA + 3006AOLA, AR3006D): Spell; Wait(1); IncrementGlobal; counter 1 -> 4.
S.stepCaster{name = 'Aola', cre = 'AOLA', script = '3006AOLA', last = 4}
