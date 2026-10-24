local API = {}
local GetTime = GetTime
NorthlightSettingsAPI = API
local frame, pending, serial = CreateFrame("Frame", "NorthlightSettingsTransport"), nil, 0
frame:SetParent(nil); frame:Show()
NorthlightSettingsDiagnostics = type(NorthlightSettingsDiagnostics) == "table" and NorthlightSettingsDiagnostics or {}
local diagnostics = NorthlightSettingsDiagnostics
function API.RecordError(stage, reason) diagnostics.lastError = stage .. ": " .. tostring(reason):sub(1, 512); diagnostics.errorAt = GetTime() end
local status = {}
local session = tostring(math.floor(GetTime() * 1000000)) .. "-" .. tostring(math.random(100000, 999999))
local CHUNK = 768
local cachedSnapshot

local function split(text)
    local out = {}
    for value in (text .. "\t"):gmatch("(.-)\t") do out[#out + 1] = value:gsub("%%(%x%x)", function(hex) return string.char(tonumber(hex, 16)) end) end
    return out
end
local function snapshot(text)
    if #text > 524288 then error("Northlight snapshot is too large") end
    local result = { fields = {} }
    for line in text:gmatch("[^\n]+") do
        local p = split(line)
        if p[1] == "NLS1" then result.revision, result.activeRevision = p[2], p[3]
        elseif p[1] == "CAP" and p[2] == "HDR" then result.hdrStatus, result.hdrCode, result.hdrReason = p[3], p[4], p[5]
        elseif p[1] == "CAP" and p[2] == "AOPreview" then result.aoPreview = p[3] == "1"
        elseif p[1] == "F" and #p == 18 then
            if #result.fields >= 2048 then error("Too many Northlight settings") end
            if not (p[2] == "Q" or p[2] == "R" or p[2] == "C" or p[2] == "S") or
               not (p[5] == "check" or p[5] == "slider" or p[5] == "rgb" or p[5] == "enum") or
               not tonumber(p[6]) or not tonumber(p[7]) or not tonumber(p[8]) then error("Invalid Northlight field") end
            result.fields[#result.fields + 1] = {
                file = p[2], section = p[3], key = p[4], kind = p[5], min = tonumber(p[6]), max = tonumber(p[7]), step = tonumber(p[8]),
                value = p[9], active = p[10], factory = p[11], override = p[12], apply = p[13], options = p[14], origin = p[15],
                id = p[2] .. "\t" .. p[3] .. "\t" .. p[4],
                inherited = p[16], factoryOverride = p[17],
                available = p[18] == "1",
            }
            local f = result.fields[#result.fields]
            if f.file == "Q" and (f.key == "AutoBumpTerrain" or f.key == "AutoBumpWalls" or f.key == "AutoBumpGrass" or f.key == "AutoBumpModels") then
                f.presetOptions, f.kind = f.options, "enum"
                f.options = f.max >= 3 and "0,1,2,3" or f.max >= 2 and "0,1,2" or "0,1"
            end
            if f.file == "Q" and f.key == "AutoHeightPolarity" then f.presetOptions, f.kind, f.options = f.options, "enum", "0,1,2" end
        end
    end
    if not result.revision or #result.fields == 0 then error("Invalid Northlight snapshot") end
    return result
end
local function finish(ok, value)
    local job = pending
    pending = nil
    status[job.id] = { state = ok and "complete" or "error", result = value }
    diagnostics.phase = ok and "complete" or "error"
    if not ok then API.RecordError("request", value) end
    diagnostics[job.input == "snapshot" and "lastSnapshotSeconds" or job.input:sub(1, 11) == "preview-ao\t" and "lastPreviewSeconds" or "lastCommitSeconds"] = GetTime() - job.start
    local called, why = pcall(job.callback, ok, value)
    if not called then API.RecordError("callback", why) end
end
local function send()
    local j = pending
    SetCVar("northlightSettingsRequest", "NL1\t" .. j.id .. "\t" .. j.part .. "\t" .. j.parts .. "\t" .. j.page .. "\n" .. j.input:sub((j.part - 1) * CHUNK + 1, j.part * CHUNK))
end
local function submit(input, callback)
    if pending then return nil, "Northlight is processing another request" end
    if #input > 131072 then return nil, "Northlight request exceeds 128 KiB" end
    local ok, why = pcall(function()
        RegisterCVar("northlightSettingsRequest", "")
        RegisterCVar("northlightSettingsReply", "")
    end)
    if not ok then return nil, tostring(why) end
    serial = serial + 1
    if serial > 32 then status[session .. "-" .. (serial - 32)] = nil end
    local id = session .. "-" .. serial
    pending = { id = id, input = input, part = 1, parts = math.max(1, math.ceil(#input / CHUNK)), page = 1, output = {}, callback = callback, start = GetTime(), progress = GetTime(), acknowledged = 0 }
    diagnostics.phase, diagnostics.requestId, diagnostics.page, diagnostics.pages = "request", id, 1, nil
    status[id] = { state = "pending" }
    local sent, reason = pcall(send)
    if not sent then pending = nil; API.RecordError("send", reason); status[id] = {state = "error", result = tostring(reason)}; return nil, tostring(reason) end
    return id
end
local function poll()
    if not pending then return end
    if GetTime() - pending.progress > 5 or GetTime() - pending.start > 120 then
        finish(false, "Northlight did not respond (page " .. pending.page .. "). If saving, the result is unknown: refresh before retrying.")
        return
    end
    local reply = GetCVar("northlightSettingsReply") or ""
    local header, body = reply:match("^([^\n]+)\n(.*)$")
    if not header then return end
    local p, j = split(header), pending
    if p[1] ~= "NL1" or p[2] ~= j.id then return end
    local accepted, page, pages = tonumber(p[3]), tonumber(p[4]), tonumber(p[5])
    diagnostics.phase, diagnostics.page, diagnostics.pages = "reply", page, pages
    if not accepted or not page or not pages or pages > 683 then finish(false, "Invalid Northlight response"); return end
    if accepted > j.acknowledged then j.acknowledged, j.progress = accepted, GetTime() end
    if accepted == j.part and j.part < j.parts then j.part = j.part + 1; send(); return end
    if pages == 0 then return end
    if page ~= j.page or #body > CHUNK then return end
    j.output[page], j.progress = body, GetTime()
    if page < pages then j.page = page + 1; send(); return end
    local output = table.concat(j.output)
    if output:sub(1, 6) == "ERROR\t" then finish(false, output:sub(7):gsub("\n$", "")) else finish(true, output) end
end
local lastPoll = -1
function API.Poll()
    if not pending then return end
    local now = GetTime(); if now - lastPoll < .01 then return end; lastPoll = now
    diagnostics.heartbeat = now
    local ok, why = pcall(poll)
    if not ok then API.RecordError("poll", why); if pending then finish(false, "Northlight transport error: " .. tostring(why)) end end
end
frame:SetScript("OnUpdate", API.Poll)
-- The visible native window also drives the request if another UI hides the transport frame.
VideoOptionsFrame:HookScript("OnUpdate", API.Poll)

function API.GetCachedSnapshot()
    if cachedSnapshot then diagnostics.cacheHit = true; return cachedSnapshot end
    if type(NorthlightSettingsCache) ~= "table" or NorthlightSettingsCache.version ~= 9 or type(NorthlightSettingsCache.text) ~= "string" then return end
    local ok, result = pcall(snapshot, NorthlightSettingsCache.text)
    if ok then cachedSnapshot = result; diagnostics.cacheHit = true; return result end
    NorthlightSettingsCache = nil
end
function API.GetSnapshot(callback)
    return submit("snapshot", function(ok, text)
        if not ok then callback(false, text); return end
        local started = GetTime()
        local valid, value = pcall(snapshot, text)
        diagnostics.lastParseSeconds = GetTime() - started
        if valid then cachedSnapshot, NorthlightSettingsCache = value, { version = 9, text = text } end
        callback(valid, value)
    end)
end
function API.CancelSnapshot()
    if not pending or pending.input ~= "snapshot" then return false end
    -- Abandon only a read. The next request has a new ID, so late pages cannot reach its callback.
    status[pending.id] = { state = "cancelled" }
    pending = nil
    diagnostics.phase = "cancelled"
    return true
end
function API.CancelPreview()
    if not pending or pending.input:sub(1, 11) ~= "preview-ao\t" then return false end
    status[pending.id] = { state = "cancelled" }
    pending = nil
    return true
end
function API.PreviewAO(method, enabled, callback)
    local input = "preview-ao\t" .. method .. (method == "saved" and "" or "\t" .. (enabled and "1" or "0"))
    return submit(input, function(ok, text)
        if not ok then callback(false, text); return end
        local valid = text == "PREVIEW\t" .. method .. "\n"
        callback(valid, valid and method or "Invalid AO preview confirmation")
    end)
end
function API.Commit(baseRevision, changes, callback)
    return submit("commit\t" .. baseRevision .. "\n" .. changes, function(ok, text)
        if not ok then callback(false, text); return end
        local revision = text:match("^SAVED\t([^\t\r\n]+)\n$")
        if revision then cachedSnapshot, NorthlightSettingsCache = nil, nil end
        callback(revision ~= nil, revision or "Invalid Northlight save confirmation; refresh before retrying.")
    end)
end
function API.GetStatus(id) return status[id] end
