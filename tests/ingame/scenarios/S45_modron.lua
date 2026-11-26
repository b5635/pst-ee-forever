-- scenario: save=000700120 env=PSTF_TBC_AUTOPARTY=1 timeout=600
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S45 Modron Maze (AR13FY): this save has one modron in reach (the plan's 12 rogue modrons are elsewhere in the
-- maze). Made hostile in the tight corridors: paths, no push, no stuck turn, the fight ends.
S.brawl{name = 'Modron', provoke = false, hostile = true, maxFrames = 6000}
