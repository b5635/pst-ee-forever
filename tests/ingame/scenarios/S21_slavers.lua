-- scenario: save=000700186 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S21 Thokola slavers (0900SLV1-3 on THKSLAV*, AR0900): a shared counter AR0900Curst_Slavers, each step
-- "FaceObject; FloatMessage; Wait(2); SetGlobal(next)" behind See([EA=2]). Set to 24 with the slavers hostile: in a
-- fight each SetGlobal sits behind a Wait at its turn end; the counter must still step 25, 26, 27, 28.
S.plot{name = 'Thokola Slaver', provoke = false, hostile = true, hp = 400, partyHp = true,
	set = {{'Curst_Slavers', 'AR0900', 24}},
	expect = {{'Curst_Slavers', 'AR0900', 25}, {'Curst_Slavers', 'AR0900', 26}, {'Curst_Slavers', 'AR0900', 27}, {'Curst_Slavers', 'AR0900', 28}},
	maxFrames = 6000}
