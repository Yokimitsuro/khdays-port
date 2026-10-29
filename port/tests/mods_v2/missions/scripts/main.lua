-- The missions walk: what the mod hears of its own mission (its number past the game's) as it starts and ends.
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
