-- Vehicle Lua stops while the game is paused, so keep EngineSim linked from here

local M = {}

local udp
local timer = 0

local function onUpdate(dtReal)
  if not simTimeAuthority.getPause() then return end
  timer = timer + dtReal
  if timer < 0.1 then return end
  timer = 0
  if not udp then
    udp = socket.udp()
    udp:settimeout(0)
    udp:setpeername("127.0.0.1", 4570)
  end
  udp:send("BESH")
end

local function onExtensionUnloaded()
  if udp then udp:close() end
end

M.onUpdate = onUpdate
M.onExtensionUnloaded = onExtensionUnloaded

return M
