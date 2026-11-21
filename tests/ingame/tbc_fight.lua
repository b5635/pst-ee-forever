-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game turn-based fight: makes the creature nearest the protagonist hostile (engine Enemy()) and
-- walks the protagonist to it, which must start a turn-based fight. Run with party AI so it plays
-- out with no input:
--   tools\launch.ps1 -Lua tests\ingame\tbc_fight.lua -Env @{PSTF_TBC_AUTOPARTY='1'} -Until 'tbc: fight 1 over' -Timeout 300 -Close
-- The turn log (tbc: lines, with HP after every turn) is the result.
PSTF_TbcState()           -- idle
local id = PSTF_MakeEnemyNearest()
if id == -1 then error("no creature to make hostile") end
local x, y = PSTF_SpritePos(id)
-- The player starts it: the protagonist (party slot 0) walks up; within start_radius the fight begins
-- and party AI (NearestEnemyOf needs the enemy in sight) attacks on each party turn.
C:Eval('MoveToPoint([' .. math.floor(x) .. '.' .. math.floor(y + 40) .. '])', 0)
