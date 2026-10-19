-- Vehicle side of the bridge: sends rpm, throttle and ignition to engine-sim, applies its torque,
-- and swaps the player car's engine/exhaust blends for silent markers the DSP plugin replaces.

local M = {}

local ffi = require("ffi")
pcall(ffi.cdef, [[
typedef struct { char magic[4]; float rpm, throttle, ignition, volume, pan; } engineSimOut_t;
typedef struct { char magic[4]; float torque, rpm; } engineSimIn_t;
]])

local enginePort = 4570
local useEngineSimTorque = true

local outPacket = ffi.new("engineSimOut_t")
local inPacket = ffi.new("engineSimIn_t")
local profileValues = ffi.new("float[256]")
ffi.copy(outPacket.magic, "BES1", 4)

local udp
local engine
local origTorqueUpdate
local esTorque, esAge = 0, math.huge
local active = false
local originalBlends = {}
local appliedProfileId
local stockEngine

local markerBlends = {
  engine = "art/sound/engineSim/engineSimEngine.sfxBlend2D.json",
  exhaust = "art/sound/engineSim/engineSimExhaust.sfxBlend2D.json",
}

local function exhaustNodePairs()
  local nodePairs = {}
  for _, v in pairs(engine.thermals and engine.thermals.exhaustEndNodes or {}) do
    table.insert(nodePairs, {v.start, v.finish})
  end
  if #nodePairs == 0 then nodePairs = {{engine.engineNodeID, engine.engineNodeID}} end
  return nodePairs
end

-- shortcut: restoring re-inits with gains 1/1 instead of the jbeam's on/offLoadGain; fine as a
-- toggle, read soundConfig from v.data if the restored sound level matters
local function useMarkerBlends(on)
  local cfg = engine.soundConfiguration
  if not cfg then return end
  if engine.engineSoundID and cfg.engine then
    originalBlends.engine = originalBlends.engine or cfg.engine.blendFile
    engine:initEngineSound(engine.engineSoundID, on and markerBlends.engine or originalBlends.engine, {engine.engineNodeID}, 1, 1, "engine")
  end
  if engine.engineSoundIDExhaust and cfg.exhaust then
    originalBlends.exhaust = originalBlends.exhaust or cfg.exhaust.blendFile
    engine:initExhaustSound(engine.engineSoundIDExhaust, on and markerBlends.exhaust or originalBlends.exhaust, exhaustNodePairs(), 1, 1, "exhaust")
  end
end

local tunedFields = {"torqueCurve", "idleRPM", "idleAV", "maxRPM", "maxAV", "revLimiterRPM", "revLimiterAV",
  "revLimiterMaxAV", "invRevLimiterRange", "inertia", "initialInertia", "invEngInertia", "halfInvEngInertia",
  "friction", "initialFriction", "dynamicFriction", "engineBrakeTorque", "torqueData", "maxPower", "maxTorque", "maxPowerThrottleMap"}

local function refreshTorqueData()
  engine.torqueData = engine:getTorqueData()
  engine.maxPower = engine.torqueData.maxPower
  engine.maxTorque = engine.torqueData.maxTorque
  engine.maxPowerThrottleMap = engine.torqueData.maxPower * 735.499
end

local function restoreStockEngine()
  if stockEngine then
    for _, k in ipairs(tunedFields) do engine[k] = stockEngine[k] end
  end
  appliedProfileId = nil
end

-- p (0-based): id, idle rpm, limit rpm, inertia, friction, dynamic friction, n, rpm[n], gross torque[n]
local function applyProfile(p, count)
  local n = p[6]
  if n < 2 or count < 7 + 2 * n then return end
  if not stockEngine then
    stockEngine = {}
    for _, k in ipairs(tunedFields) do stockEngine[k] = engine[k] end
  end
  local idleRPM, limitRPM, rpmToAV = p[1], p[2], 0.104719755

  local curve, j = {}, 0
  for rpm = 0, math.floor(limitRPM + 1000) do
    while j < n - 2 and p[7 + j + 1] < rpm do j = j + 1 end
    local r0, r1 = p[7 + j], p[7 + j + 1]
    local t = clamp((rpm - r0) / (r1 - r0), 0, 1)
    local torque = p[7 + n + j] + (p[7 + n + j + 1] - p[7 + n + j]) * t
    if rpm < idleRPM * 0.3 then torque = 0 end
    if rpm > limitRPM then torque = torque * math.max(1 - (rpm - limitRPM) / 1000, 0) end
    curve[rpm] = torque
  end
  engine.torqueCurve = curve

  engine.friction, engine.initialFriction = p[4], p[4]
  engine.dynamicFriction = p[5]
  engine.engineBrakeTorque = 0
  engine.idleRPM, engine.idleAV = idleRPM, idleRPM * rpmToAV
  engine.maxRPM, engine.maxAV = limitRPM, limitRPM * rpmToAV
  if engine.hasRevLimiter then
    engine.revLimiterRPM, engine.revLimiterAV = limitRPM, limitRPM * rpmToAV
    if engine.revLimiterMaxAVOvershoot then
      engine.revLimiterMaxAV = engine.maxAV + engine.revLimiterMaxAVOvershoot
      engine.invRevLimiterRange = 1 / engine.revLimiterMaxAVOvershoot
    end
  end
  local inertia = clamp(p[3], 0.02, 2)
  engine.inertia, engine.initialInertia = inertia, inertia
  engine.invEngInertia = 1 / inertia
  engine.halfInvEngInertia = 0.5 / inertia
  refreshTorqueData()
  appliedProfileId = p[0]
end

local function torqueUpdate(device, dt)
  origTorqueUpdate(device, dt)
  if not (active and useEngineSimTorque and esTorque and esAge < 0.5) then return end
  -- Starting, stalling and engine-off stay pure BeamNG: engine-sim torque near 0 rpm is
  -- mostly compression spikes, and feeding it back spun the crank backwards.
  if device.ignitionCoef <= 0 or device.isStalled or device.outputAV1 < device.idleAV * 0.6 then return end

  local beamTorque = device.lastOutputTorque - device.frictionTorque
  local dAV = dt * (esTorque - beamTorque) * device.invEngInertia * device.outputAVState
  for i = 1, device.activeOutputPortCount do
    local name = device.outputAVNames[device.activeOutputPorts[i]]
    device[name] = math.max(device[name] + dAV, 0)
  end
end

local function updateGFX(dt)
  if not engine then return end

  -- reset re-selects the engine's update functions, so re-wrap when that happens
  if engine.torqueUpdate ~= torqueUpdate then
    origTorqueUpdate = engine.torqueUpdate
    engine.torqueUpdate = torqueUpdate
    appliedProfileId = nil
  end

  esAge = esAge + dt
  while true do
    local data = udp:receive()
    if not data then break end
    local tag = #data == ffi.sizeof(inPacket) and data:sub(1, 4)
    if tag == "BES1" then
      ffi.copy(inPacket, data, #data)
      esTorque = clamp(inPacket.torque, -0.5 * engine.maxTorque, 1.5 * engine.maxTorque)
      esAge = 0
    elseif tag == "BESN" then
      esTorque = nil
      esAge = 0
    elseif data:sub(1, 4) == "BESP" and #data <= 4 + 4 * 256 then
      local count = math.floor((#data - 4) / 4)
      ffi.copy(profileValues, data:sub(5), count * 4)
      if active and profileValues[0] == 1 then
        if profileValues[1] ~= appliedProfileId then
          local p = {} -- 0-based copy without the enabled flag (no ffi pointer math in BeamNG)
          for i = 1, count - 1 do p[i - 1] = profileValues[i] end
          applyProfile(p, count - 1)
        end
      elseif appliedProfileId then
        restoreStockEngine()
      end
    end
  end

  local seated = playerInfo.firstPlayerSeated == true
  if seated ~= active then useMarkerBlends(seated) end
  if not seated and appliedProfileId then restoreStockEngine() end
  active = seated
  if not active then return end

  outPacket.rpm = math.abs(engine.outputAV1) * 9.549296596
  -- Pedal only: BeamNG's randomized idle-controller pulses made engine-sim surge at idle
  outPacket.throttle = math.min(engine.requestedThrottle or 0, engine.throttle or 0)
  outPacket.ignition = (engine.ignitionCoef or 0) > 0 and 1 or 0
  outPacket.volume = 1
  outPacket.pan = 0
  udp:send(ffi.string(outPacket, ffi.sizeof(outPacket)))
end

local function onExtensionLoaded()
  engine = powertrain.getDevicesByType("combustionEngine")[1]
  if not engine then return false end

  udp = socket.udp()
  udp:settimeout(0)
  udp:setpeername("127.0.0.1", enginePort)
end

local function onExtensionUnloaded()
  if active then useMarkerBlends(false) end
  if engine and appliedProfileId then restoreStockEngine() end
  if engine and engine.torqueUpdate == torqueUpdate then engine.torqueUpdate = origTorqueUpdate end
  if udp then udp:close() end
end

M.onExtensionLoaded = onExtensionLoaded
M.onExtensionUnloaded = onExtensionUnloaded
M.updateGFX = updateGFX

return M
