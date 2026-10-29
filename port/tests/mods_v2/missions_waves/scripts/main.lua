-- The missions walks: what the mod hears of its own mission (its number past the game's) as it starts and ends.
local FIRST_NEW = 3510
local playing = false

mod.on("mission_start", function(e)
  playing = e.mission >= FIRST_NEW
  if playing then print("the mod's mission", e.mission) end
end)

mod.on("mission_end", function(e)
  if playing then print("mission end", e.ending, e.rank) end
  playing = false
end)

-- The player's HP set to what it is on every frame from the field's first: refused while the field loads (the
-- player is there before the gauge that shows the HP), then done.
local told = {}
mod.on("frame", function(e)
  local hp = player.hp
  if hp == nil then return end
  local done, why = player.set_hp(hp)
  local said = done and "done" or why
  if not told[said] then
    told[said] = true
    print("set_hp in the field", said)
  end
end)
