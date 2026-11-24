-- SPDX-License-Identifier: GPL-2.0-or-later
-- Showcase: gore close-up (README recording). Real-time melee on one Hive thug (kept alive); every
-- ~0.7 s a hit of each blood type (gush 3/4/5, artery 6, crits) lands as the engine's AddBlood would,
-- between the party's own hits; ends with a critical exploding death (7). Waits ~15 s (launch overlays).
local n, foe, t0, gap = 0, -1, 800, 24
local seq = {{3,0},{4,0},{5,1},{6,0},{4,1},{5,0},{3,1},{6,1},{5,1},{4,1}}
local last = t0 + 40 + #seq * gap
function pstfTestTick()
  n = n + 1
  if n == t0 then
    foe = PSTF_MakeEnemyNearest()
    PSTF_CenterOn(foe)
    for s = 0, 7 do
      local p = PSTF_PartyId(s)
      if p ~= -1 then PSTF_EvalAs(p, 'Attack(NearestEnemyOf(Myself))') end
    end
    PSTF_Note('blood: start')
  end
  if foe ~= -1 and n < last and n % 10 == 0 then PSTF_SetHp(foe, 300) PSTF_EvalAs(foe, "Wait(1)", 1) end
  local k = n - t0 - 40
  if k > 0 and k % gap == 0 and k <= #seq * gap then
    local s = seq[math.floor(k / gap)]
    if s then PSTF_GoreHit(foe, s[1], s[2]) end
  end
  if n == last + 10 then PSTF_GoreHit(foe, 7, 1) PSTF_SetHp(foe, 1) end
  if n == last + 150 then PSTF_Note('blood: done') pstfTestTick = nil end
end
