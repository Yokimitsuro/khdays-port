-- The forms walk's deck (port/tests/mods_v2/character_forms): at the select, the Zero Gear (230) alone with UNITS
-- Ability Units (241) linked on its cells; the camp then chooses Twin's form from it.
local ZERO_GEAR, UNIT = 230, 241
local UNITS = 3
local set = false

local function index(page, row, col)
  return (page - 1) * 40 + (row - 1) * 5 + col
end

mod.on("scene", function(e)
  if e.to ~= 7 or set then return end
  set = true
  local info = deck.info(ZERO_GEAR)
  local minr, minc = 0, 0
  for _, c in ipairs(info.cells) do
    if c.row < minr then minr = c.row end
    if c.col < minc then minc = c.col end
  end
  local row0, col0 = 1 - minr, 1 - minc
  local saved = deck.save()
  local grid = {}
  for i = 1, #saved.grid do grid[i] = 0 end
  grid[index(1, row0, col0)] = ZERO_GEAR
  local linked = 0
  for _, c in ipairs(info.cells) do
    if linked < UNITS and (c.row ~= 0 or c.col ~= 0) then
      grid[index(1, row0 + c.row, col0 + c.col)] = UNIT
      linked = linked + 1
    end
  end
  local owned = {}
  for id, n in pairs(saved.owned) do owned[id] = n end
  owned[ZERO_GEAR] = math.max(owned[ZERO_GEAR] or 0, 1)
  owned[UNIT] = math.max(owned[UNIT] or 0, UNITS)
  print("forms deck", deck.restore({grid = grid, owned = owned}), "units", linked)
end)
