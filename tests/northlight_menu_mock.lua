-- Stock 5.1 API surface; no client, filesystem API or injected Lua globals.
local objects, now, cvars = {}, 0, {}
local methods = {}
local noops = {"EnableMouse","EnableKeyboard","SetJustifyH","SetMinMaxValues","SetValueStep","SetScrollChild","SetVerticalScroll","SetOwner","AddLine","SetAutoFocus","SetMaxLetters","SetTextInsets","SetFontObject","SetNormalFontObject","SetHighlightFontObject","SetTexture","SetVertexColor","HighlightText"}
for _, name in ipairs(noops) do methods[name] = function() end end
function methods:SetMinMaxValues(low, high) self.low, self.high = low, high end
function methods:SetWidth(n)
    if SIMULATE_LAYOUT_ERROR and self.frameParent and self.frameParent.name == "NorthlightSettingsScroll" then error("injected layout failure") end
    self.width = n
end
function methods:SetHeight(n) self.height = n end
function methods:GetWidth()
    if self.width then return self.width end
    if self.allPoints then return self.allPoints:GetWidth() end
    local left, right = self.points and self.points.TOPLEFT, self.points and self.points.BOTTOMRIGHT
    if left and right and self.frameParent then return self.frameParent:GetWidth() - (left[1] or 0) + (right[1] or 0) end
    return PANEL_WIDTH or 420
end
function methods:SetParent(p) self.frameParent = p end
function methods:SetAllPoints(p) self.allPoints = p end
function methods:ClearAllPoints() self.points = {}; self.allPoints = nil end
function methods:SetPoint(point, a, b, c, d)
    self.points = self.points or {}; self.points[point] = type(a) == "number" and {a,b} or {c,d}
end
function methods:GetHeight() return self.height or 20 end
function methods:SetFrameLevel(n) self.level = n end
function methods:GetFrameLevel() return self.level or 1 end
function methods:SetText(s) self.text = s; if self.scripts.OnTextChanged then self.scripts.OnTextChanged(self, false) end end
function methods:TypeText(s) self.text = s; if self.scripts.OnTextChanged then self.scripts.OnTextChanged(self, true) end end
function methods:GetText() return self.text end
function methods:SetHitRectInsets(...) self.hitInsets = {...} end
function methods:SetTextColor() end
function methods:SetAlpha() end
function methods:Enable() self.disabled = false end
function methods:Disable() self.disabled = true end
function methods:SetScript(name, fn) self.scripts[name] = fn end
function methods:GetScript(name) return self.scripts[name] end
function methods:HookScript(name, fn) local old = self.scripts[name]; self.scripts[name] = function(...) if old then old(...) end; fn(...) end end
function methods:Show()
    if VideoOptionsFrame and VideoOptionsFrame.categories and (self == VideoOptionsFrame.northlight or self.parent == "Northlight") then
        for _, child in pairs(VideoOptionsFrame.categories) do if child ~= self then child:Hide() end end
        if self ~= VideoOptionsFrame.northlight then VideoOptionsFrame.northlight:Hide() end
    end
    local was = self.shown; self.shown = true; if not was and self.scripts.OnShow then self.scripts.OnShow(self) end
end
function methods:Hide() local was = self.shown; self.shown = false; if was and self.scripts.OnHide then self.scripts.OnHide(self) end end
function methods:IsShown() return self.shown end
function methods:GetName() return self.name end
function methods:SetValue(n) self.value = n; if self.scripts.OnValueChanged then self.scripts.OnValueChanged(self, n) end end
function methods:SetChecked(v) self.checked = v end
function methods:GetChecked() return self.checked end
local editFocus
function methods:SetFocus()
    if editFocus == self then return end
    if editFocus then editFocus:ClearFocus() end
    editFocus = self; if self.scripts.OnEditFocusGained then self.scripts.OnEditFocusGained(self) end
end
function methods:ClearFocus()
    if editFocus ~= self then return end
    editFocus = nil; if self.scripts.OnEditFocusLost then self.scripts.OnEditFocusLost(self) end
end
local function object(name)
    local o = setmetatable({ name = name, scripts = {}, shown = true }, { __index = methods }); objects[#objects + 1] = o; if name then _G[name] = o end; return o
end
function methods:CreateFontString(name) local o = object(name); o.frameParent = self; return o end
function methods:CreateTexture(name) local o = object(name); o.frameParent = self; return o end
function CreateFrame(kind, name, parent, template)
    local o = object(name); o.frameParent = parent
    o.transport = name == "NorthlightSettingsTransport" or not name and not parent
    if template == "OptionsSliderTemplate" then for _, suffix in ipairs({"Low", "High", "Text"}) do object(name .. suffix) end end
    return o
end
function GetTime() return now end
function GetLocale() return TEST_LOCALE or "enUS" end
function RegisterCVar(k, v) if not cvars[k] then cvars[k] = v end end
function GetCVar(k) return cvars[k] end
function UIDropDownMenu_SetWidth() end
function UIDropDownMenu_Initialize(o, fn) o.initialize = fn end
function UIDropDownMenu_SetText(o, s) o.text = s end
function UIDropDownMenu_EnableDropDown(o) o.disabled = false end
function UIDropDownMenu_DisableDropDown(o) o.disabled = true end
function UIDropDownMenu_CreateInfo() return {} end
local menuItems = {}
function UIDropDownMenu_AddButton(info) menuItems[#menuItems + 1] = info end
function CloseDropDownMenus() end
GameFontNormalSmall, GameFontHighlightSmall = {}, {}
StaticPopupDialogs = {}
function StaticPopup_Show(which, text, _, data)
    local dialog = object(); dialog.which, dialog.data, dialog.text = which, data, string.format(StaticPopupDialogs[which].text, text)
    dialog:SetScript("OnHide", function(self) local info = StaticPopupDialogs[self.which]; if info.OnHide then info.OnHide(self, self.data) end end)
    LAST_POPUP = dialog; return dialog
end
function StaticPopup_Hide(which) if LAST_POPUP and LAST_POPUP.which == which then LAST_POPUP:Hide() end end
function AcceptPreset()
    local dialog = assert(LAST_POPUP); assert(dialog:IsShown())
    StaticPopupDialogs[dialog.which].OnAccept(dialog, dialog.data); dialog:Hide()
end
function CancelPreset() assert(LAST_POPUP):Hide() end
GameTooltip = object()
VideoOptionsFrame = object("VideoOptionsFrame")
VideoOptionsFramePanelContainer = object()
VideoOptionsFrameApply = object("VideoOptionsFrameApply")
VideoOptionsFrameApply:Disable()
VideoOptionsFrameOkay = object("VideoOptionsFrameOkay")
VideoOptionsFrameCancel = object("VideoOptionsFrameCancel")
local originalCalls, commits, weather, revision = 0, 0, "1", "r1"
VideoOptionsFrameApply:SetScript("OnClick", function(self) originalCalls = originalCalls + 1; self:Disable() end)
VideoOptionsFrameOkay:SetScript("OnClick", function() originalCalls = originalCalls + 1; VideoOptionsFrame:Hide() end)
function OptionsFrame_AddCategory(frame, panel)
    assert(frame == VideoOptionsFrame)
    frame.categories = frame.categories or {}
    if panel.parent then assert(panel.parent == "Northlight"); frame.categories[panel.name] = panel else frame.northlight = panel end
end
local savedAO, previewMethod, previewEnabled, failPreview = "SSAO", "SSAO", true, false
local savedAOQuality = "High"
local function field(file, section, key, kind, value, apply, options, available, factory)
    return table.concat({"F", file, section, key, kind, "0", "1", "1", value, value, factory or "1", value, apply or "F", options or "", "f", value, factory or "1", available == false and "0" or "1"}, "\t") .. "\n"
end
local function fixture()
    if NATIVE_FIXTURE then return NATIVE_FIXTURE:gsub("^NLS1[^\n]+", "NLS1\t" .. revision .. "\t" .. revision) end
    return "NLS1\t" .. revision .. "\t" .. revision .. "\n" ..
        field("Q", "Quality", "Preset", "enum", "Quality", "R", "Quality,Balanced,Performance", nil, "Quality") ..
        field("Q", "Quality", "Weather", "check", weather, "F", "1,1,1") .. field("Q", "Quality", "GI", "check", "1", "R", "1,1,1") ..
        field("Q", "Quality", "AOMethod", "enum", savedAO, "F", "SSAO,HDAO,HBAOPlus,GTAO", nil, "SSAO") ..
        field("Q", "Quality", "AOQuality", "enum", savedAOQuality, "F", "High,Medium,Low", nil, "High") ..
        field("R", "Renderer", "Backend", "enum", "native", "R", "native,dxvk,dxvk2,legacy", nil, "dxvk") .. field("R", "Renderer", "ExperimentalHDR", "check", "0", "R", "", HDR_AVAILABLE) .. (EXTRA_FIELDS or "") .. (SNAPSHOT_PADDING or "") .. "CAP\tHDR\tinactive\t0\n" .. (AO_PREVIEW_AVAILABLE and "CAP\tAOPreview\t1\n" or "")
end
local live, failSave = true, false
local server = {}
local queuedReply, replyReady
function SetCVar(k, v)
    cvars[k] = v
    if not live then return end
    local id, part, count, page, body = v:match("^NL1\t([^\t]+)\t(%d+)\t(%d+)\t(%d+)\n(.*)$")
    if not id then return end
    part, count, page = tonumber(part), tonumber(count), tonumber(page)
    if server.id ~= id then server = { id = id, accepted = 0, input = "" } end
    if part == server.accepted + 1 then server.input = server.input .. body; server.accepted = part end
    if part == count and not server.output then
        if server.input == "snapshot" then server.output = fixture()
        elseif server.input:sub(1, 11) == "preview-ao\t" then
            local method, enabled = server.input:match("^preview%-ao\t([^\t]+)\t([01])$")
            if failPreview then server.output = "ERROR\tPreview unavailable\n"
            elseif server.input == "preview-ao\tsaved" then previewMethod, previewEnabled = savedAO, true; server.output = "PREVIEW\tsaved\n"
            else
                assert(method == "SSAO" or method == "HDAO" or method == "HBAOPlus" or method == "GTAO")
                previewMethod, previewEnabled = method, enabled == "1"; server.output = "PREVIEW\t" .. method .. "\n"
            end
        elseif failSave then server.output = "ERROR\tINI changed externally\n"
        else
            assert(server.input:match("^commit\t([^\n]+)") == revision, "save used a stale revision")
            commits = commits + 1
            LAST_COMMIT = server.input
            local new = server.input:match("Q\tQuality\tWeather\t(%d)")
            if new then weather = new end
            savedAO = server.input:match("Q\tQuality\tAOMethod\t([^\n]+)") or savedAO
            savedAOQuality = server.input:match("Q\tQuality\tAOQuality\t([^\n]+)") or savedAOQuality
            previewMethod, previewEnabled = savedAO, true
            revision = "r" .. (commits + 1); server.output = "SAVED\t" .. revision .. "\n"
        end
    end
    local output = server.output or ""; local pages = math.ceil(#output / 768)
    page = math.min(page, pages)
    local reply = "NL1\t" .. id .. "\t" .. part .. "\t" .. page .. "\t" .. pages .. "\n" .. (page > 0 and output:sub((page - 1) * 768 + 1, page * 768) or "")
    if NATIVE_DELAY then queuedReply, replyReady = reply, now + NATIVE_DELAY else cvars.northlightSettingsReply = reply end
end
function CheckAOPreview()
    NATIVE_DELAY = .08
    previewMethod = "HDAO" -- A Lua reload must clear an earlier unsaved preview.
    local panel = assert(VideoOptionsFrame.northlight); panel:Show(); Tick(60)
    assert(previewMethod == "SSAO" and commits == 0, "initial load must reset an abandoned preview")
    NorthlightSection_lighting:Show()
    local function row(key)
        for _, o in ipairs(objects) do if o.field and o.field.key == key and o:IsShown() then return o end end
        error("AO row missing: " .. key)
    end
    local function choose(method)
        local o = row("AOMethod"); menuItems = {}; o.dropdown.initialize()
        for _, item in ipairs(menuItems) do if item.text == method or method == "HBAOPlus" and item.text == "HBAO+ (D3D9)" then item.func(); return end end
        error("AO option missing")
    end
    for _, method in ipairs({"GTAO", "HDAO", "HBAOPlus", "SSAO"}) do
        choose(method); Tick(3)
        assert(previewMethod == method and commits == 0 and originalCalls == 0, "AO must preview without saving or Apply")
    end
    choose("HDAO"); choose("GTAO"); Tick(3)
    assert(previewMethod == "GTAO" and commits == 0, "rapid AO selection must use the latest request")
    local ao = row("ContactAO"); ao.check:SetChecked(false); ao.check:GetScript("OnClick")(ao.check); Tick(3)
    assert(not previewEnabled and commits == 0, "AO enable/disable must preview too")
    panel.cancel(); Tick(3); assert(previewMethod == "SSAO" and previewEnabled and savedAO == "SSAO")
    NorthlightSection_atmosphere:Show()
    local weatherRow = FindWeatherRow(); weatherRow.check:SetChecked(false); weatherRow.check:GetScript("OnClick")(weatherRow.check)
    NorthlightSection_lighting:Hide(); NorthlightSection_lighting:Show(); choose("HDAO"); Tick(3)
    assert(weather == "1" and commits == 0, "AO preview saved unrelated draft settings")
    panel.cancel(); Tick(3)
    choose("GTAO")
    VideoOptionsFrameApply:GetScript("OnClick")(VideoOptionsFrameApply, "LeftButton"); Tick(3)
    assert(commits == 1 and savedAO == "GTAO" and originalCalls == 1 and weather == "1", "Apply must save the chosen method once")
    choose("HDAO"); Tick(3); VideoOptionsFrame:Hide(); Tick(3)
    assert(previewMethod == "GTAO" and commits == 1, "closing Video must restore the saved technique")
    VideoOptionsFrame:Show(); failPreview = true; choose("SSAO"); Tick(3)
    assert(commits == 1 and VideoOptionsFrame:IsShown(), "preview errors must leave the draft editable")
    failPreview = false; panel.cancel(); Tick(3); assert(previewMethod == "GTAO")
    print("AO live preview: all four methods, rapid selection, enable/disable, Apply, Cancel, close, errors and unrelated drafts passed")
end
function Tick(n)
    for _ = 1, n do
        local step = TICK_STEP or .051
        now = now + step
        if queuedReply and now >= replyReady then cvars.northlightSettingsReply, queuedReply = queuedReply, nil end
        for _, o in ipairs(objects) do if o.scripts.OnUpdate and not (HIDE_TRANSPORT and o.transport) and not (REPLACE_VIDEO_UPDATE and o == VideoOptionsFrame) then o.scripts.OnUpdate(o, step) end end
    end
end
function CheckAOQuality()
    local panel = assert(VideoOptionsFrame.northlight); panel:Show(); Tick(30)
    NorthlightSection_lighting:Show()
    local function choose(value)
        local row = FindSetting("AOQuality"); menuItems = {}; row.dropdown.initialize()
        for _, item in ipairs(menuItems) do if item.text == value then item.func(); return end end
        error("AO quality option missing: " .. value)
    end
    choose("Medium"); Tick(3); assert(commits == 0 and savedAOQuality == "High")
    panel.cancel(); Tick(3); assert(savedAOQuality == "High" and commits == 0)
    choose("Low"); VideoOptionsFrameApply:GetScript("OnClick")(VideoOptionsFrameApply, "LeftButton"); Tick(3)
    assert(commits == 1 and savedAOQuality == "Low" and savedAO == "SSAO" and weather == "1")
    assert(("\n" .. LAST_COMMIT .. "\n"):find("\nQ\tQuality\tAOQuality\tLow\n", 1, true))
    print("AO quality: opt-in dropdown, unsaved draft, Cancel, Apply and independent method/settings passed")
end
function FindWeatherRow()
    for _, o in ipairs(objects) do if o.field and o.field.key == "Weather" and o:IsShown() then return o end end
    error("Weather checkbox not found")
end
function FindSetting(key)
    for _, row in ipairs(objects) do if row.field and row.field.key == key and row:IsShown() then return row end end
    error("setting not found: " .. key)
end
function ExpandAll()
    for _ = 1, 30 do
        local more
        for _, row in ipairs(objects) do if row.rowType == "more" and row:IsShown() and row.more.text:sub(1, 2) == "+ " then more = row.more; break end end
        if not more then return end
        more:GetScript("OnClick")(more)
    end
    error("advanced disclosure did not settle")
end
function CheckMenu()
    local panel = assert(VideoOptionsFrame.northlight)
    panel:Show(); Tick(30)
    assert(NorthlightPresets:IsShown() and NorthlightPreset_Quality and NorthlightPreset_Balanced and NorthlightPreset_Performance, "presets belong on the root")
    assert(not VideoOptionsFrame.categories.Quality, "root presets must replace the old quality checkbox category")
    NorthlightSection_atmosphere:Show()
    local row = FindWeatherRow(); assert(row.check:GetChecked())
    row.check:SetChecked(false); row.check:GetScript("OnClick")(row.check)
    assert(commits == 0, "draft must not write INIs")
    panel.cancel(); assert(row.check:GetChecked() and commits == 0)
    row.check:SetChecked(false); row.check:GetScript("OnClick")(row.check)
    VideoOptionsFrameApply:GetScript("OnClick")(VideoOptionsFrameApply, "LeftButton")
    assert(originalCalls == 0, "native Apply must wait for the commit")
    Tick(60); assert(commits == 1 and weather == "0" and originalCalls == 1)
    Tick(60); assert(commits == 1 and originalCalls == 1, "polling must not repeat a commit or native Apply")
    row = FindWeatherRow(); row.check:SetChecked(true); row.check:GetScript("OnClick")(row.check)
    failSave = true
    VideoOptionsFrameOkay:GetScript("OnClick")(VideoOptionsFrameOkay, "LeftButton"); Tick(30)
    assert(originalCalls == 1 and VideoOptionsFrame:IsShown(), "an error must leave the panel open and the draft intact")
    panel.cancel(); assert(not row.check:GetChecked())
    NorthlightSection_renderer:Show()
    for _, o in ipairs(objects) do if o.field and o:IsShown() then assert(o.field.key == "Backend", "HDR leaked into Renderer") end end
    NorthlightSection_hdr:Show()
    local found = false
    for _, o in ipairs(objects) do if o.field and o.field.key == "ExperimentalHDR" and o:IsShown() then
        if HDR_AVAILABLE then assert(not o.check.disabled and not o.disabledReason) else assert(o.check.disabled and o.reset.disabled and o.disabledReason) end
        found = true
    end end
    assert(found, "HDR availability must follow the native consumer metadata")
    local hdrRows = {}
    for _, o in ipairs(objects) do if o.field and o:IsShown() then hdrRows[o.field.key] = o; assert(o.field.key ~= "Backend") end end
    assert(hdrRows.HDRContrast and hdrRows.HDRExposure, "HDR grading sliders missing")
    if HDR_AVAILABLE then
        hdrRows.ExperimentalHDR.check:SetChecked(true); hdrRows.ExperimentalHDR.check:GetScript("OnClick")(hdrRows.ExperimentalHDR.check)
        assert(not hdrRows.HDRExposure.disabledReason and not hdrRows.HDRContrast.disabledReason)
        hdrRows.HDRExposure.sliders[1]:SetValue(-1.25); hdrRows.HDRContrast.sliders[1]:SetValue(1.35)
        assert(_G[hdrRows.HDRExposure.sliders[1]:GetName() .. "Text"].text == "-1.25")
        panel.cancel()
    end
    assert(not NorthlightFileMenu, "file filtering must be replaced with native child categories")
    local seen = {}
    for _, child in pairs(VideoOptionsFrame.categories) do
        child:Hide(); child:Show(); ExpandAll()
        for _, row in ipairs(objects) do if row.field and row:IsShown() then
            seen[row.field.id] = true
            if row.field.key == "PointShadowLimit" then
                local slider = row.sliders[1]
                assert(slider.low == 4 and slider.high == 128, "shadow slider range must be 4..128")
                slider:SetValue(128); assert(_G[slider:GetName() .. "Text"].text == "128")
                slider:SetValue(4); assert(_G[slider:GetName() .. "Text"].text == "4")
            end
            assert(row:GetWidth() <= (PANEL_WIDTH or 420) - 48, "row exceeds the visible panel")
            if row.rowType == "field" and row.field.kind == "slider" then
                local slider, edit = row.sliders[1], row.edits[1]
                local x = slider.points.TOPLEFT[1]
                assert(x >= row.label:GetWidth() + 10 and x + slider:GetWidth() <= edit.points.TOPLEFT[1] - 5 and edit.points.TOPLEFT[1] + edit:GetWidth() <= row:GetWidth(), "slider/input overlap or escape panel")
                assert(row:GetHeight() == 28 and slider.hitInsets[3] == 0 and slider.hitInsets[4] == 0, "dense sliders must not retain overlapping mouse hit areas")
            end
            assert(row.rowType == "header" or row.label.text ~= row.field.key or row.field.key == "GI", "technical key leaked into the player label: " .. row.field.key)
        end end
    end
    for key in (EXPECTED_KEYS or ""):gmatch("%S+") do assert(seen["Q\tQuality\t" .. key], "setting missing from semantic sections: " .. key) end
    failSave, live = false, false
    local finished = false
    assert(NorthlightSettingsAPI.GetSnapshot(function(ok, result) finished = true; assert(not ok and result:find("did not respond")) end))
    Tick(650); assert(finished)
    print("Lua 5.1 menu: native category, draft/cancel, async Apply, error handling, exactly-once callback and missing-DLL timeout passed")
end
function CheckLayoutFailure()
    SIMULATE_LAYOUT_ERROR = true
    local panel = assert(VideoOptionsFrame.northlight); panel:Show(); Tick(60)
    local found = false
    for _, o in ipairs(objects) do if o.text and o.text:find("injected layout failure", 1, true) then found = true end end
    assert(found, "a UI callback failure must replace processing with an error")
    SIMULATE_LAYOUT_ERROR = false; panel:Hide(); panel:Show(); Tick(30)
    NorthlightSection_atmosphere:Show()
    assert(FindWeatherRow(), "the panel must recover after a failed layout")
    print("Lua UI failure: visible error and retry recovery passed")
end
function CheckApplyLatency()
    -- Hundreds of regional records plus a delayed CVar pump reproduce the real round trip.
    local padding = {}
    for i = 1, 900 do padding[i] = field("C", "Azeroth:" .. i, "sun_disc_gain", "slider", "1") end
    SNAPSHOT_PADDING, NATIVE_DELAY = table.concat(padding), .08
    local panel = assert(VideoOptionsFrame.northlight); panel:Show(); Tick(500)
    NorthlightSection_atmosphere:Show()
    local row = FindWeatherRow()
    local function edit(value) row = FindWeatherRow(); row.check:SetChecked(value); row.check:GetScript("OnClick")(row.check) end
    local function apply() VideoOptionsFrameApply:GetScript("OnClick")(VideoOptionsFrameApply, "LeftButton") end
    edit(false); local start = now; apply()
    assert(originalCalls == 0 and VideoOptionsFrameCancel.disabled, "commit itself must remain guarded")
    assert(not NorthlightSettingsAPI.CancelSnapshot(), "a write must never be abandoned")
    Tick(3)
    assert(commits == 1 and originalCalls == 1, "Apply must finish on SAVED without waiting for the full snapshot")
    assert(not VideoOptionsFrameCancel.disabled and not FindWeatherRow().check:GetChecked())
    edit(true)
    assert(VideoOptionsFrameApply.disabled == false, "controls must accept new edits while refreshing")
    Tick(500)
    assert(FindWeatherRow().check:GetChecked(), "background refresh must retain edits made after Apply")
    panel.cancel(); assert(not FindWeatherRow().check:GetChecked(), "Cancel must restore the acknowledged value")
    edit(true); apply(); Tick(3)
    assert(commits == 2 and originalCalls == 2)
    -- A second Apply supersedes a read-only refresh; it cannot queue behind its remaining pages.
    edit(false); apply(); Tick(3)
    assert(commits == 3 and originalCalls == 3 and weather == "0", "repeat Apply waited for the background snapshot")
    Tick(500); assert(commits == 3 and originalCalls == 3 and not FindWeatherRow().check:GetChecked())
    edit(true); failSave = true; apply(); Tick(3)
    assert(commits == 3 and originalCalls == 3 and FindWeatherRow().check:GetChecked(), "failed save must preserve the draft")
    panel.cancel(); assert(not FindWeatherRow().check:GetChecked())
    failSave = false; edit(true)
    VideoOptionsFrameOkay:GetScript("OnClick")(VideoOptionsFrameOkay, "LeftButton"); live = false; Tick(3)
    assert(commits == 4 and originalCalls == 4 and not VideoOptionsFrame:IsShown(), "Okay must close on SAVED")
    Tick(650); VideoOptionsFrame:Show(); panel:Hide(); panel:Show()
    NorthlightSection_atmosphere:Show()
    assert(FindWeatherRow().check:GetChecked(), "a background timeout must retain the acknowledged value")
    edit(false); panel.cancel(); assert(FindWeatherRow().check:GetChecked(), "a background timeout must not block editing or Cancel")
    print("Delayed Lua transport: Apply unlocks within 0.153 s, repeated Apply, background edits, Cancel, save error and Okay passed")
end
function CheckApplyEditing()
    local padding = {}
    for i = 1, 900 do padding[i] = field("C", "Azeroth:" .. i, "sun_disc_gain", "slider", "1") end
    SNAPSHOT_PADDING, NATIVE_DELAY = table.concat(padding), .08
    local panel = assert(VideoOptionsFrame.northlight); panel:Show(); Tick(500)
    local function weatherEdit(value)
        NorthlightSection_atmosphere:Show()
        local row = FindWeatherRow(); row.check:SetChecked(value); row.check:GetScript("OnClick")(row.check)
    end
    local function apply() VideoOptionsFrameApply:GetScript("OnClick")(VideoOptionsFrameApply, "LeftButton") end
    weatherEdit(false); NATIVE_DELAY = 2; apply()
    assert(commits == 1 and originalCalls == 0 and VideoOptionsFrameCancel.disabled)
    weatherEdit(true)
    assert(FindWeatherRow().check:GetChecked(), "Apply must accept edits before the save acknowledgement")
    NorthlightSection_materials:Show(); ExpandAll()
    FindSetting("AutoBumpStrength").sliders[1]:SetValue(105)
    assert(FindSetting("AutoBumpStrength").edits[1]:GetText() == "105", "sliders must remain editable during Apply")
    local mode = FindSetting("AutoBumpTerrain").dropdown; menuItems = {}; mode.initialize()
    for _, item in ipairs(menuItems) do if item.text == "Normal maps" then item.func(); break end end
    assert(FindSetting("AutoBumpTerrain").dropdown.text == "Normal maps", "dropdowns must remain editable during Apply")
    assert(VideoOptionsFrameApply.disabled and VideoOptionsFrameOkay.disabled, "writes must remain serialized")
    apply(); panel.cancel()
    assert(commits == 1 and originalCalls == 0 and not NorthlightSettingsAPI.CancelSnapshot(), "pending saves cannot be repeated or cancelled")
    Tick(40); NATIVE_DELAY = .08
    assert(originalCalls == 1 and not VideoOptionsFrameApply.disabled, "the native Apply callback must not disable the next draft")
    Tick(500); weatherEdit(true)
    assert(FindWeatherRow().check:GetChecked(), "save acknowledgement and refresh must preserve the next draft")
    apply(); Tick(3)
    assert(commits == 2 and weather == "1" and LAST_COMMIT:find("AutoBumpStrength\t105", 1, true), "next Apply must submit the new draft at the acknowledged revision")
    Tick(500); weatherEdit(false); failSave = true; NATIVE_DELAY = 2; apply(); weatherEdit(true)
    Tick(40); NATIVE_DELAY = .08
    assert(commits == 2 and FindWeatherRow().check:GetChecked(), "save failure must preserve a newer edit of the submitted field")
    panel.cancel(); assert(FindWeatherRow().check:GetChecked(), "Cancel must restore the last acknowledged settings")
    failSave = false; weatherEdit(false); live = false; apply(); weatherEdit(true); Tick(110)
    assert(commits == 2 and FindWeatherRow().check:GetChecked() and not VideoOptionsFrameCancel.disabled, "timeout must preserve newer edits without retrying a write")
    print("Apply editing: immediate checkbox/slider/dropdown edits, delayed acknowledgement, next revision, native callback, failure and timeout passed")
end
function CheckAcknowledgedInheritance()
    local function inherit(line, options)
        local parts = {}; for v in (line:gsub("\n$", "") .. "\t"):gmatch("(.-)\t") do parts[#parts + 1] = v end
        parts[12], parts[14], parts[17] = "~", options or "", "~"
        return table.concat(parts, "\t") .. "\n"
    end
    EXTRA_FIELDS = inherit(field("Q", "Quality", "FogClouds", "check", "1"), "1,1,0") ..
        field("C", "default", "sun_disc_gain", "slider", "1") ..
        inherit(field("C", "Azeroth:12", "sun_disc_gain", "slider", "1"))
    NATIVE_DELAY = .08
    local panel = assert(VideoOptionsFrame.northlight); panel:Show(); Tick(30)
    local function find(key)
        for _, o in ipairs(objects) do if o.field and o.field.key == key and o:IsShown() then return o end end
        error("missing inherited row: " .. key)
    end
    -- Confirmed root presets must remain the saved baseline during the background snapshot.
    NorthlightPreset_Performance:GetScript("OnClick")(); AcceptPreset(); NorthlightSection_atmosphere:Show()
    assert(not find("FogClouds").check:GetChecked())
    VideoOptionsFrameApply:GetScript("OnClick")(VideoOptionsFrameApply, "LeftButton"); Tick(2)
    assert(originalCalls == 1 and not find("FogClouds").check:GetChecked())
    panel.cancel(); assert(not find("FogClouds").check:GetChecked())
    NorthlightSection_celestial:Show()
    find("sun_disc_gain").sliders[1]:SetValue(0)
    VideoOptionsFrameApply:GetScript("OnClick")(VideoOptionsFrameApply, "LeftButton"); Tick(2)
    assert(originalCalls == 2)
    menuItems = {}; NorthlightRegionMenu.initialize()
    for _, item in ipairs(menuItems) do if item.text == "Azeroth:12" then item.func() end end
    local region = find("sun_disc_gain"); assert(region.sliders[1].value == 0, "region did not inherit the acknowledged default")
    region.sliders[1]:SetValue(1); panel.cancel()
    assert(region.sliders[1].value == 0, "Cancel lost the acknowledged inherited default")
    print("Acknowledged preset/default inheritance survives Apply, background refresh and Cancel")
end

function CheckLoadingRecovery()
    local padding = {}
    for i = 1, 700 do padding[i] = field("C", "Azeroth:" .. i, "sun_disc_gain", "slider", "1") end
    SNAPSHOT_PADDING, NATIVE_DELAY, TICK_STEP = table.concat(padding), .012, .016
    HIDE_TRANSPORT, REPLACE_VIDEO_UPDATE = true, true
    local panel = assert(VideoOptionsFrame.northlight); panel:Show(); Tick(210)
    NorthlightSection_atmosphere:Show()
    assert(FindWeatherRow(), "large initial snapshot did not load within 3.36 seconds on the visible-panel pump")
    assert(NorthlightSettingsCache and NorthlightSettingsCache.version == 9 and #NorthlightSettingsCache.text > 40000)
    local started = now; panel:Hide(); panel:Show()
    NorthlightSection_atmosphere:Show()
    assert(FindWeatherRow() and now == started, "reopening the menu must use its parsed snapshot immediately")
    live = false
    local callbacks = 0
    assert(NorthlightSettingsAPI.GetSnapshot(function(ok, reason)
        callbacks = callbacks + 1; assert(not ok and reason:find("did not respond"))
    end))
    Tick(330)
    assert(callbacks == 1, "a silent bridge must fail within five seconds without freezing the controls")
    Tick(20); assert(callbacks == 1)
    local row = FindWeatherRow(); row.check:SetChecked(false); row.check:GetScript("OnClick")(row.check)
    panel.cancel(); assert(FindWeatherRow().check:GetChecked(), "bridge timeout disabled editing or Cancel")
    print("Loading: 700 regional fields in <=3.36 s, owned visible pump, instant reopen, five-second stall timeout and editable recovery passed")
end
function ExportCache() return NorthlightSettingsCache.text end
function CheckBumpMenu()
    local panel = assert(VideoOptionsFrame.northlight); panel:Show(); Tick(60)
    assert(NorthlightSection_materials, "materials category is missing")
    NorthlightSection_materials:Show(); Tick(3)
    local function find(key)
        for _, row in ipairs(objects) do if row.field and row.field.key == key and row:IsShown() then return row end end
        error("missing bump control: " .. key)
    end
    local function toggle(key, value) local row=find(key); row.check:SetChecked(value); row.check:GetScript("OnClick")(row.check) end
    local function selectMode(key, value)
        local row=find(key); assert(row.field.kind == "enum"); menuItems={}; row.dropdown.initialize()
        for _, option in ipairs(menuItems) do if option.text == value then option.func(); return end end
        error("missing material mode: " .. key .. " / " .. value)
    end
    find("AutoModelDisplacementStrength").sliders[1]:SetValue(40);find("AutoModelDisplacementDistance").sliders[1]:SetValue(10);find("AutoModelDisplacementDetail").sliders[1]:SetValue(3)
    selectMode("AutoBumpModels","Normal maps + POM");selectMode("AutoHeightPolarity","Dark is high");find("AutoHeightDetails").sliders[1]:SetValue(90);find("AutoHeightBalance").sliders[1]:SetValue(60)
    toggle("AutoParallaxShadows",false)
    assert(find("AutoParallaxShadowStrength").disabledReason and find("AutoParallaxShadowSteps").disabledReason)
    assert(not find("AutoCavityStrength").disabledReason, "self-shadow toggle must not disable cavity")
    toggle("AutoCavity",false);assert(find("AutoCavityStrength").disabledReason)
    panel.cancel();assert(find("AutoParallaxShadows").check:GetChecked() and find("AutoCavity").check:GetChecked())
    find("AutoParallaxShadowStrength").sliders[1]:SetValue(85);find("AutoParallaxShadowSteps").sliders[1]:SetValue(12);find("AutoCavityStrength").sliders[1]:SetValue(55)
    find("AutoDisplacementStrength").sliders[1]:SetValue(125);find("AutoDisplacementDistance").sliders[1]:SetValue(16);find("AutoDisplacementDetail").sliders[1]:SetValue(5)
    toggle("AutoBump", false)
    for _, key in ipairs({"AutoDisplacementStrength","AutoDisplacementDistance","AutoDisplacementDetail","AutoBumpStrength","AutoBumpDistance","AutoBumpTerrain","AutoBumpWalls","AutoBumpGrass","AutoParallaxShadows","AutoParallaxShadowStrength","AutoParallaxShadowSteps","AutoCavity","AutoCavityStrength"}) do assert(find(key).disabledReason, "bump dependency did not disable " .. key) end
    panel.cancel(); assert(find("AutoBump").check:GetChecked(), "Cancel failed to restore bump enable")
    find("AutoParallaxShadowStrength").sliders[1]:SetValue(85);find("AutoParallaxShadowSteps").sliders[1]:SetValue(12);find("AutoCavityStrength").sliders[1]:SetValue(55)
    find("AutoBumpStrength").sliders[1]:SetValue(105);find("AutoBumpDistance").sliders[1]:SetValue(24)
    find("AutoDisplacementStrength").sliders[1]:SetValue(125);find("AutoDisplacementDistance").sliders[1]:SetValue(16);find("AutoDisplacementDetail").sliders[1]:SetValue(5)
    find("AutoModelDisplacementStrength").sliders[1]:SetValue(40);find("AutoModelDisplacementDistance").sliders[1]:SetValue(10);find("AutoModelDisplacementDetail").sliders[1]:SetValue(3)
    selectMode("AutoBumpModels","Normal maps + POM");selectMode("AutoHeightPolarity","Dark is high");find("AutoHeightDetails").sliders[1]:SetValue(90);find("AutoHeightBalance").sliders[1]:SetValue(60)
    selectMode("AutoBumpTerrain","Displacement + normal maps");selectMode("AutoBumpWalls","Normal maps");selectMode("AutoBumpWalls","Normal maps + POM");selectMode("AutoBumpWalls","Off");selectMode("AutoBumpGrass","Off");toggle("AutoBump",false)
    VideoOptionsFrameApply:GetScript("OnClick")(VideoOptionsFrameApply,"LeftButton");Tick(3)
    for key,value in pairs({AutoBumpModels="2",AutoModelDisplacementStrength="40",AutoModelDisplacementDistance="10",AutoModelDisplacementDetail="3",AutoHeightPolarity="2",AutoHeightDetails="90",AutoHeightBalance="60",AutoDisplacementStrength="125",AutoDisplacementDistance="16",AutoDisplacementDetail="5",AutoBump="0",AutoBumpStrength="105",AutoBumpDistance="24",AutoBumpTerrain="3",AutoBumpWalls="0",AutoBumpGrass="0",AutoParallaxShadowStrength="85",AutoParallaxShadowSteps="12",AutoCavityStrength="55"}) do
        assert(LAST_COMMIT and LAST_COMMIT:find("Q\tQuality\t" .. key .. "\t" .. value,1,true), "bump Apply did not serialize " .. key)
    end
    assert(commits==1 and originalCalls==1)
    print("Material addon: modes, independent POM shadows/cavity, strengths, steps, dependencies, Cancel and Apply transport passed")
end
function CheckCachedLoading()
    live, HIDE_TRANSPORT, REPLACE_VIDEO_UPDATE = false, true, true
    local panel = assert(VideoOptionsFrame.northlight); panel:Show()
    assert(NorthlightPresets:IsShown(), "cached root presets must display immediately")
    NorthlightSection_atmosphere:Show()
    local row = FindWeatherRow(); assert(row.check:GetChecked(), "saved snapshot must display before any CVar response")
    row.check:SetChecked(false); row.check:GetScript("OnClick")(row.check)
    Tick(110)
    assert(not FindWeatherRow().check:GetChecked(), "failed cache verification destroyed an edit")
    panel.cancel(); assert(FindWeatherRow().check:GetChecked())
    assert(NorthlightSettingsDiagnostics.lastError:find("did not respond"))
    print("Reload: validated saved cache displays synchronously, unavailable bridge preserves draft and Cancel")
end
function CheckPresetConfirmation()
    EXTRA_FIELDS = EXTRA_FIELDS:gsub("(F\tQ\tQuality\tLocalLightLimit\t[^\n]+)", function(line)
        local p = {}; for v in (line .. "\t"):gmatch("(.-)\t") do p[#p + 1] = v end
        p[12], p[17] = "~", "~"; return table.concat(p, "\t")
    end) .. field("C", "default", "sun_disc_gain", "slider", "3", "F", "", nil, "1") .. field("C", "Azeroth:12", "sun_disc_gain", "slider", "2", "F", "", nil, "1")
    local panel = VideoOptionsFrame.northlight; panel:Show(); Tick(60)
    NorthlightSection_atmosphere:Show()
    local weatherRow = FindWeatherRow(); weatherRow.check:SetChecked(false); weatherRow.check:GetScript("OnClick")(weatherRow.check)
    panel:Show(); NorthlightPreset_Balanced:GetScript("OnClick")()
    assert(LAST_POPUP.data == "Balanced" and LAST_POPUP.text:find("ALL Northlight settings", 1, true), "preset confirmation must name the full replacement")
    assert(commits == 0 and originalCalls == 0)
    VideoOptionsFrameApply:GetScript("OnClick")(VideoOptionsFrameApply, "LeftButton")
    assert(commits == 0 and originalCalls == 0, "Apply must be blocked while confirmation is open")
    CancelPreset(); NorthlightSection_atmosphere:Show()
    assert(not FindWeatherRow().check:GetChecked(), "modal Cancel must preserve the previous draft")
    panel.cancel(); panel:Show(); NorthlightPreset_Performance:GetScript("OnClick")(); AcceptPreset()
    assert(commits == 0, "confirming a preset must not bypass native Apply")
    NorthlightSection_atmosphere:Show(); assert(not FindSetting("FogClouds").check:GetChecked())
    NorthlightSection_materials:Show(); assert(FindSetting("AutoBumpTerrain").dropdown.text == "Normal maps" and FindSetting("AutoBumpWalls").dropdown.text == "Normal maps", "material enum must inherit the pending preset")
    NorthlightSection_renderer:Show(); assert(FindSetting("Backend").dropdown.text == "dxvk", "presets must reset renderer settings too")
    NorthlightSection_celestial:Show(); assert(FindSetting("sun_disc_gain").edits[1]:GetText() == "1", "presets must reset regional/default settings too")
    NorthlightSection_performance:Show()
    local limit = FindSetting("LocalLightLimit"); assert(limit.edits[1]:GetText() == "24")
    limit.edits[1]:SetFocus(); limit.edits[1]:SetText("48"); limit.edits[1]:GetScript("OnEnterPressed")(limit.edits[1])
    assert(FindSetting("LocalLightLimit").edits[1]:GetText() == "48", "a manual value equal to the old baseline must override the pending preset")
    VideoOptionsFrameApply:GetScript("OnClick")(VideoOptionsFrameApply, "LeftButton"); Tick(8)
    assert(commits == 1 and originalCalls == 1)
    for _, entry in ipairs({"Q\tQuality\tPreset\tPerformance", "Q\tQuality\tGI\t~", "Q\tQuality\tLocalLightLimit\t48", "R\tRenderer\tBackend\t@factory", "C\tdefault\tsun_disc_gain\t@factory", "C\tAzeroth:12\tsun_disc_gain\t@factory"}) do assert(LAST_COMMIT:find(entry, 1, true), "missing preset replacement: " .. entry) end
    print("Root presets: full-replacement confirmation, Cancel, blocked background Apply, all sections, pending inheritance override and one commit passed")
end
function CheckNumericInputs()
    EXTRA_FIELDS = EXTRA_FIELDS .. "F\tC\tdefault\tsun_disc\trgb\t0\t1\t0.01\t1,0.9,0.8\t1,0.9,0.8\t1,1,1\t1,0.9,0.8\tF\t\tf\t1,1,1\t1,1,1\t1\n"
    local panel = VideoOptionsFrame.northlight; panel:Show(); Tick(60); NorthlightSection_materials:Show()
    local function enter(key, text)
        local row = FindSetting(key); row.edits[1]:SetFocus(); row.edits[1]:SetText(text); row.edits[1]:GetScript("OnEnterPressed")(row.edits[1]); return FindSetting(key)
    end
    assert(enter("AutoBumpStrength", "999").edits[1]:GetText() == "150", "numeric input must clamp to schema bounds")
    local row = FindSetting("AutoBumpStrength"); row.edits[1]:SetFocus(); row.edits[1]:SetText("12"); row.edits[1]:GetScript("OnEscapePressed")()
    assert(FindSetting("AutoBumpStrength").edits[1]:GetText() == "150", "Escape must discard unfinished typing")
    panel.cancel(); enter("AutoBumpStrength", "invalid")
    assert(FindSetting("AutoBumpStrength").edits[1]:GetText() == "75" and commits == 0, "invalid input must restore the current value")
    row = FindSetting("AutoBumpStrength"); row.edits[1]:SetFocus(); row.edits[1]:TypeText("105"); panel.refresh()
    assert(row.edits[1]:GetText() == "105", "a background redraw must not overwrite unfinished typing")
    local main = FindSetting("AutoBump"); main.check:SetChecked(false); main.check:GetScript("OnClick")(main.check)
    assert(not FindSetting("AutoBump").check:GetChecked(), "flushing a focused number must not undo the clicked checkbox")
    VideoOptionsFrameApply:GetScript("OnClick")(VideoOptionsFrameApply, "LeftButton"); Tick(6)
    assert(LAST_COMMIT:find("Q\tQuality\tAutoBumpStrength\t105", 1, true) and LAST_COMMIT:find("Q\tQuality\tAutoBump\t0", 1, true))
    panel.cancel(); NorthlightSection_celestial:Show()
    local color = FindSetting("sun_disc"); color.edits[2]:SetFocus(); color.edits[2]:SetText("0.35"); color.edits[2]:GetScript("OnEnterPressed")(color.edits[2])
    assert(FindSetting("sun_disc").edits[1]:GetText() == "1" and FindSetting("sun_disc").edits[2]:GetText() == "0.35" and FindSetting("sun_disc").edits[3]:GetText() == "0.8", "RGB editing changed another channel")
    panel.cancel(); NorthlightSection_materials:Show()
    local enable = FindSetting("AutoBump"); enable.check:SetChecked(true); enable.check:GetScript("OnClick")(enable.check)
    row = FindSetting("AutoBumpDistance"); row.edits[1]:SetFocus(); row.edits[1]:TypeText("32")
    assert(not VideoOptionsFrameApply.disabled, "typing must enable native Apply before focus is lost")
    VideoOptionsFrameApply:GetScript("OnClick")(VideoOptionsFrameApply, "LeftButton"); Tick(6)
    assert(LAST_COMMIT:find("Q\tQuality\tAutoBumpDistance\t32", 1, true), "Apply must commit a still-focused numeric edit")
    print("Numeric controls: schema clamping, Escape, invalid text, focused Apply/checkbox, and independent compact RGB channels passed")
end
function CheckFullLayout()
    local panel = VideoOptionsFrame.northlight; panel:Show(); Tick(150)
    local seen = {}
    local function inspect()
        ExpandAll()
        local bottom = 0
        for _, row in ipairs(objects) do if row.rowType and row:IsShown() then
            local top = -row.points.TOPLEFT[2]
            assert(top >= bottom, "rows overlap vertically"); bottom = top + row:GetHeight()
            assert(row:GetHeight() <= 28, "a compact row grew beyond 28 UI units")
            if row.field then
                seen[row.field.id] = true
                assert(row.field.key ~= "Preset", "preset dropdown leaked into a section")
                if row.rowType == "field" and (row.field.kind == "slider" or row.field.kind == "rgb") then
                    for c = 1, row.field.kind == "rgb" and 3 or 1 do
                        local edit = row.edits[c]; assert(edit.points.TOPLEFT[1] - 5 >= row.label:GetWidth() + 5 and edit.points.TOPLEFT[1] + edit:GetWidth() <= row:GetWidth(), "native input border escapes its column")
                        if row.field.kind == "rgb" then assert(edit.points.TOPLEFT[1] - 5 >= row.channels[c].points.TOPLEFT[1] + 10, "native input border overlaps RGB channel label") end
                    end
                end
            end
        end end
    end
    for _, child in pairs(VideoOptionsFrame.categories) do child:Show(); inspect() end
    for _, child in ipairs({NorthlightSection_celestial, NorthlightSection_range}) do
        child:Show(); menuItems = {}; NorthlightRegionMenu.initialize()
        local regions = menuItems
        for _, item in ipairs(regions) do item.func(); inspect() end
    end
    for id in NATIVE_EXPECTED_IDS:gmatch("[^\n]+") do assert(seen[id], "real cached setting missing from the layout: " .. id) end
    print("Real cached schema: every setting/region reachable, bounded native controls, dense rows and no overlapping layout passed")
end
