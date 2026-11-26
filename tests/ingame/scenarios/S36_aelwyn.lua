-- scenario: save=000700130 env=PSTF_TBC_AUTOPARTY=1 timeout=500
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S36 Aelwyn (0600AELN, AR0600): attacked in a fight -> FloatMessage, AR0600 Attack_Aelwyn = 1, Dialogue(Player1). The dialogue starts (she walks up over turns), then the fight goes on.
S.talker{name = 'Aelwyn', provoke = false, expect = {{'Attack_Aelwyn', 'AR0600', 1}}}
