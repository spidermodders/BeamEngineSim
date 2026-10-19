
local M = {}

local loadCommand = 'extensions.load("engineSimBridge")'

local function onExtensionLoaded()
  for i = 0, be:getObjectCount() - 1 do
    be:getObject(i):queueLuaCommand(loadCommand)
  end
end

local function onVehicleSpawned(vid)
  local veh = be:getObjectByID(vid)
  if veh then veh:queueLuaCommand(loadCommand) end
end

M.onExtensionLoaded = onExtensionLoaded
M.onVehicleSpawned = onVehicleSpawned

return M
