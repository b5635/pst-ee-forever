-- scenario: save=000700136 env=PSTF_TBC_AUTOPARTY=1 timeout=500
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S37 Splinter (0601SPLI, AR0601): attacked -> GLOBAL Call_MK = 1, GLOBAL Splinter = 1, Dialogue([EA=2]).
S.talker{name = 'Splinter', provoke = false, expect = {{'Splinter', 'GLOBAL', 1}}}
