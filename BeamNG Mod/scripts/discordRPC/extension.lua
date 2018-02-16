-- This Source Code Form is subject to the terms of the bCDDL, v. 1.1.
-- If a copy of the bCDDL was not distributed with this
-- file, You can obtain one at http://beamng.com/bCDDL-1.1.txt

local M = {}

local locale = nil

local ip = '127.0.0.1'
local port = 8888

local udpSocket = nil

local loaded = false
local listenForStateChanges = false
local lastState = "freeroam"

local vehicleInfoCache = {}
local partConfigInfoCache = {}

local function getLocString(str)
  if str == nil then return str end
  if locale ~= nil and locale[str] ~= nil then return locale[str] end
  return str
end

local function tableCount(tbl)
  local c = 0
  for k,v in pairs(tbl) do
    c = c + 1
  end
  return c
end

local function tableIndex(key, tbl)
  local tidx = 0
  for k,v in pairs(tbl) do
    if key == k then
      return tidx
    end
    tidx = tidx + 1
  end
  return -1
end

local function getCampaignInfo(campaign)
  local subsection = campaign.meta.subsections[campaign.state.activeSubsection]
  local count = tableCount(subsection.locations)
  local current = tableIndex(campaign.state.scenarioKey, subsection.locations)
  return current, count
end

local inGarage = false
local function setRpcInfo()
  local scenario = scenario_scenarios and scenario_scenarios.getScenario()
  local campaign = campaign_campaigns and campaign_campaigns.getCampaign()
  if campaign then
    --local cScenarioIdx, cScenarioCount = getCampaignInfo(campaign)
    udpSocket:sendto('state|campaign', ip, port)
    udpSocket:sendto('exif|' .. getLocString(campaign.meta.title), ip, port)
  elseif scenario then
    local isQuick = scenario.isQuickRace or false
    if isQuick then
      udpSocket:sendto('state|quickrace', ip, port)
    else
      udpSocket:sendto('state|scenario', ip, port)
    end
    udpSocket:sendto('exif|' .. getLocString(scenario.name), ip, port)
  elseif inGarage then
    udpSocket:sendto('state|garage', ip, port)
  else
    udpSocket:sendto('state|freeroam', ip, port)
    udpSocket:sendto('exif|null', ip, port)
  end
end

local function onClientStartMission(mission)
  --update locals
  loaded = true
  listenForStateChanges = true
  lastState = core_gamestate.state.state
  
  --fix broken image keys
  if string.sub(mission, 1, 1) ~= "/" then
    mission = "/" .. mission
  end
  
  --get level info
  local levelPath = path.split(mission):lower()
  local levelName = levelPath:sub(9, -2)
  local levelInfo = readJsonFile(levelPath .. "info.json")
  
  --fix broken image keys ^ 2
  if string.sub(levelName, 1, 1) == "/" then
    levelName = levelName:sub(2)
  end
  
  --update status
  inGarage = (levelName == "garage")
  setRpcInfo()
  
  --update level
  udpSocket:sendto('level|' .. levelName, ip, port)
  if inGarage then
    udpSocket:sendto('levelname|Garage', ip, port)
  else
    udpSocket:sendto('levelname|' .. (getLocString(levelInfo["title"]) or "NULL"), ip, port)
  end
  udpSocket:sendto('update', ip, port)
end
 
local function onUpdate()
  --developer debugging
  if BEAMNG_DISCORD_RPC_QUIT_SIGNAL ~= nil then
    if BEAMNG_DISCORD_RPC_QUIT_SIGNAL then
      M.quitServer()
    end
  end
  
  --check game state
  if listenForStateChanges then
    if lastState ~= core_gamestate.state.state then
      lastState = core_gamestate.state.state
      setRpcInfo()
      udpSocket:sendto('update', ip, port)
    end
  end
end

local function getPCInfoFilePath(vehicleName, pcPath)
  local removeChars = 11 + vehicleName:len()
  local jsonName = "info_" .. pcPath:sub(removeChars, -3) .. "json"
  return pcPath:sub(1, removeChars - 1) .. jsonName
end

local lastVehicleName = ""
local function setVehicle(vehicleFolder, partconfig)
  if vehicleFolder ~= lastVehicleName then
    lastVehicleName = vehicleFolder
    udpSocket:sendto('vehicle|' .. lastVehicleName:lower(), ip, port)
    
    -- try and parse vehicle name
    local infoPath = "vehicles/" .. lastVehicleName .. "/info.json"
    if FS:fileExists(infoPath) then
      --grab our info file
      local vehicleInfo = vehicleInfoCache[infoPath] or readJsonFile(infoPath)
      if vehicleInfoCache[infoPath] == nil then vehicleInfoCache[infoPath] = vehicleInfo end
      
      --send [Brand] [Name] if possible
      local vehicleDisplayName = nil
      if vehicleInfo["Brand"] ~= nil then
        vehicleDisplayName = vehicleInfo["Brand"] .. " " .. vehicleInfo["Name"]
      elseif vehicleInfo["Name"] ~= nil then
        vehicleDisplayName = vehicleInfo["Name"]
      else
        vehicleDisplayName = lastVehicleName
      end
      
      --also show the part config if possible
      if partconfig ~= nil and partconfig:len() > 0 then
        local pcJsonPath = getPCInfoFilePath(vehicleFolder, partconfig)
        if FS:fileExists(pcJsonPath) then
          local pcData = partConfigInfoCache[pcJsonPath] or readJsonFile(pcJsonPath)
          if partConfigInfoCache[pcJsonPath] == nil then partConfigInfoCache[pcJsonPath] = pcData end
          
          if pcData["Configuration"] ~= nil then
            vehicleDisplayName = vehicleDisplayName .. " " .. pcData["Configuration"]
          end
        end
      end
      
      --finally, send data
      udpSocket:sendto('vehiclename|' .. vehicleDisplayName, ip, port)
    else
      udpSocket:sendto('vehiclename|' .. lastVehicleName, ip, port)
    end
    
    udpSocket:sendto('update', ip, port)
  end
end

local function onVehicleSwitched(oldVehicleID, newVehicleID, player)
  --only do this once we've started!
  if not loaded then return end
  udpSocket:sendto('showvehicle', ip, port)
  
  local sceneVehicle = scenetree.findObjectById(newVehicleID)
  local vehicleFolder = sceneVehicle["JBeam"]
  local vehiclePartconfig = sceneVehicle["partConfig"]
  
  setVehicle(vehicleFolder, vehiclePartconfig)
end

local function setDefaultVehicle()
  --set default vehicle for freeroam loading screen
  --so players don't see Gavril D series on Discord
  if FS:fileExists("settings/default.pc") then
    local pcData = readJsonFile("settings/default.pc")
    if pcData["model"] ~= nil then
      setVehicle(pcData["model"])
    end
  end
end

local function onClientEndMission(mission)
  loaded = false
  listenForStateChanges = false
  
  udpSocket:sendto('timertype|0', ip, port)
  udpSocket:sendto('hidevehicle', ip, port) --hides vehicle during loading
  udpSocket:sendto('state|init', ip, port)
  udpSocket:sendto('init', ip, port)
  
  setDefaultVehicle() -- seems that with D series, the vehicle gets upadetd onClientStartMission
                      -- but if the user has a default vehicle, it doesn't??? This works around it
end

local function quitServer()
  udpSocket:sendto('quit', ip, port)
end

local function onExtensionLoaded()
  --load locale (maybe there's a better way to do this?)
  log('D', 'discordRPC', 'Extension loaded')
  udpSocket = socket.udp()
  udpSocket:sendto('state|init', ip, port)
  udpSocket:sendto('init', ip, port)
  locale = readJsonFile("locales/en-US.json")
  setDefaultVehicle()
end

local function onScenarioChange(scenario)
  if (scenario ~= nil and scenario.state == 'post') or (scenario == nil) then
    udpSocket:sendto('timertype|0', ip, port)
    udpSocket:sendto('update', ip, port)
  end
end

local function onScenarioRaceCountingDone()
  udpSocket:sendto('synctimer', ip, port)
  
  --check our timer type (countdown or countup)
  local scenario = scenario_scenarios.getScenario()
  udpSocket:sendto('timertype|2', ip, port)
  if scenario.goals ~= nil and scenario.goals.vehicles ~= nil then
    for k,v in pairs(scenario.goals.vehicles) do
      if v.id == "timeLimit" then
        if v.value.maxTime ~= nil then 
          udpSocket:sendto('timertype|1', ip, port)
          udpSocket:sendto('addtime|' .. tostring(math.ceil(v.value.maxTime)), ip, port)
        end
      end
    end
  end
  
  --tell Discord we havee a timer!
  udpSocket:sendto('update', ip, port)
end

local function onExit()
  quitServer()
end

--publics
M.onScenarioChange = onScenarioChange
M.onScenarioRaceCountingDone = onScenarioRaceCountingDone
M.onExit = onExit
M.onExtensionLoaded = onExtensionLoaded
M.onVehicleSwitched = onVehicleSwitched
M.onUpdate = onUpdate
M.onClientStartMission = onClientStartMission
M.onClientEndMission = onClientEndMission

--special publics
M.quitServer = quitServer

return M