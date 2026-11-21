-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game smoke test, run by tools\launch.ps1 -Lua tests\ingame\smoke.lua once a save is loaded.
-- Every PSTF_* call logs "lua: NAME -> value" to pst_forever\logs\pst_forever.log.
PSTF_Version()
PSTF_Screen()            -- expect world
PSTF_EngineMode()        -- expect 3
PSTF_RoundLength()       -- expect 51 (LoadOptions has run by now)
PSTF_RoundsForSeconds(15) -- expect 5
PSTF_WorldTime()
PSTF_TbcSim(1)
local r = PSTF_SelfTest()   -- expect ok
if r ~= "ok" then error("PSTF_SelfTest: " .. r) end
