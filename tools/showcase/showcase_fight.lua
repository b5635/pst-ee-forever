-- SPDX-License-Identifier: GPL-2.0-or-later
-- Showcase: a multi-enemy turn-based fight in the Hive (README recording). Starts after 12 s (launch overlays).
local n = 0
function pstfTestTick()
  n = n + 1
  if n < 800 then return end
  pstfTestTick = nil
  local id = PSTF_MakeEnemyNearest()
  local ids = {id, 0x180018, 0x140014, 0x1C001C}
  for i = 2, #ids do PSTF_MakeEnemy(ids[i]) end
  for _, k in ipairs(ids) do PSTF_SetHp(k, 70) end
  local x, y = PSTF_SpritePos(id)
  PSTF_Note('showcase: start')
  C:Eval('MoveToPoint([' .. math.floor(x) .. '.' .. math.floor(y + 40) .. '])', 0)
end
