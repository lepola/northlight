local API = NorthlightSettingsAPI
local function L(en) return en end
local panel = CreateFrame("Frame", "NorthlightVideoOptions", VideoOptionsFramePanelContainer)
panel.name = "Northlight"
panel:Hide()
local view = CreateFrame("Frame", nil, panel); view:SetAllPoints(panel)
local snapshot, draft, rows, busy, saving = nil, {}, {}, false, false
local confirmed, submitted, refreshing = {}, {}, false
local cached = false
local previewTarget
local file, category, section, suppress = "Q", "all", "quality", false
local refreshRows, refreshSnapshot, updateStatus, previewAO, visible
local requestPreset, focusedEdit, presetDialog
local expanded = {}
local sections = {
    {"quality", "Quality", "Q", "Preset GI ActorShadows ContactAO Weather FogClouds"},
    {"renderer", "Renderer", "R", "Backend"},
    {"hdr", "HDR", "R", "ExperimentalHDR HDRPaperWhite HDRUIWhite HDRMaxNits HDRContrast HDRExposure"},
    {"lighting", "Lighting & AO", "Q", "GI GIStrength GIRays GIBounces GIProbeMoveStep GIDynamicProbes GIThreads GIFastBVH GIDistance GIProbeAhead NightBrightness ContactAO AOMethod AOStrength AORadius EquipmentLights"},
    {"materials", "Materials & parallax", "Q", "AutoBump AutoBumpTerrain AutoBumpWalls AutoBumpGrass AutoBumpStrength AutoBumpDistance AutoParallaxDepth AutoParallaxDistance AutoParallaxSteps AutoParallaxShadows AutoParallaxShadowStrength AutoParallaxShadowSteps AutoCavity AutoCavityStrength"},
    {"shadows", "Shadows", "Q", "ActorShadows ActorShadowDetail GrassShadows ActorShadowRadius BlobShadowStrength FarShadowInterval NearShadowInterval PointShadows PointShadowLimit PointShadowRefreshMs PointShadowFacesPerFrame ShadowDirectionSteps StaticCacheSlices ShadowPivotCorrection"},
    {"atmosphere", "Atmosphere & weather", "Q", "Weather WeatherMemory RainFog FogClouds FogCloudDensity HorizonHaze HorizonHazeStart HorizonHazeBand HorizonHazeTerrain"},
    {"celestial", "Sun and moon", "C"},
    {"range", "Shadow range", "S"},
    {"spells", "Spells & names", "Q", "SpellLayer SpellLayerClasses SpellFog NameMask DepthFuncAware"},
    {"performance", "Performance", "Q", "CommandStream StreamFramesAhead StreamFrameSkip StreamOcclusionLocal ReplayJobs FrameDrawGates MinSkinnedTriangles CaptureBudgetMiB ActorShadowBudgetMiB LocalLightLimit GpuBudgetMs ActorShadowCpuMs"},
    {"diagnostics", "Diagnostics", "Q", "Diagnostics RenderProfile DiagReplayProbe ShadowFateDiagnostics SpellTrace NameMaskDebug MemMap"},
}
local bySection = {}; for _, s in ipairs(sections) do bySection[s[1]] = s end
local groups = {
    lighting = {
        {"gi", "Global illumination", "GI", "GIStrength GIDistance GIBounces", "GIRays GIProbeMoveStep GIDynamicProbes GIThreads GIFastBVH GIProbeAhead"},
        {"ao", "Ambient occlusion", "ContactAO", "AOMethod AOStrength AORadius"},
        {"ambient", "Environment", nil, "NightBrightness EquipmentLights"},
    },
    materials = {{"bump", "Material modes", "AutoBump", "AutoBumpTerrain AutoBumpWalls AutoBumpGrass AutoBumpStrength AutoBumpDistance"}, {"parallax", "POM", nil, "AutoParallaxDepth AutoParallaxDistance AutoParallaxSteps"}, {"pomshadow", "POM self-shadowing", "AutoParallaxShadows", "AutoParallaxShadowStrength AutoParallaxShadowSteps"}, {"cavity", "Cavity shading", "AutoCavity", "AutoCavityStrength"}},
    shadows = {
        {"actors", "Character shadows", "ActorShadows", "ActorShadowRadius ActorShadowDetail GrassShadows", "ActorShadowBudgetMiB ActorShadowCpuMs"},
        {"point", "Local light shadows", "PointShadows", "PointShadowLimit", "PointShadowRefreshMs PointShadowFacesPerFrame"},
        {"cache", "Shadow updates", nil, "BlobShadowStrength", "FarShadowInterval NearShadowInterval ShadowDirectionSteps StaticCacheSlices ShadowPivotCorrection"},
    },
    atmosphere = {
        {"fog", "Fog banks", "FogClouds", "FogCloudDensity"},
        {"weather", "Rain and snow", "Weather", "RainFog", "WeatherMemory"},
        {"haze", "Horizon haze", nil, "HorizonHaze HorizonHazeStart HorizonHazeTerrain", "HorizonHazeBand"},
    },
    hdr = {{"hdr", "HDR", "ExperimentalHDR", "HDRPaperWhite HDRUIWhite HDRMaxNits HDRContrast HDRExposure"}},
    celestial = {
        {"sun", "Sun", nil, "sun_disc sun_disc_gain sun_light sun_light_strength", "sun_light_mix sun_fog_gain sun_fog_height_scale sun_horizon_haze sun_glow_strength sun_glow sun_glow_mix"},
        {"moon", "Moon", nil, "moon_disc moon_disc_gain moon_light moon_light_strength", "moon_light_mix moon_fog_gain moon_fog_height_scale moon_horizon_haze moon_glow_strength moon_glow moon_glow_mix"},
    },
    spells = {
        {"spells", "Spell effects", "SpellLayer", "SpellFog", "SpellLayerClasses DepthFuncAware"},
        {"names", "Unit name protection", "NameMask", ""},
    },
    performance = {
        {"stream", "Render thread", "CommandStream", "StreamFramesAhead ReplayJobs FrameDrawGates", "StreamFrameSkip StreamOcclusionLocal"},
        {"budgets", "Resource budgets", nil, "GpuBudgetMs ActorShadowCpuMs LocalLightLimit", "MinSkinnedTriangles CaptureBudgetMiB ActorShadowBudgetMiB"},
    },
    diagnostics = {
        {"logging", "Diagnostics", "Diagnostics", "RenderProfile", "DiagReplayProbe ShadowFateDiagnostics"},
        {"debug", "Additional diagnostics", nil, "SpellTrace NameMaskDebug MemMap"},
    },
}
local presets = {
    {"Quality", "Quality", "Maximum effect detail."},
    {"Balanced", "Balanced", "Quality with a moderate effect budget."},
    {"Performance", "Performance", "Less work spent on effects."},
}
local labels = {
    AutoBump="Material detail", AutoBumpStrength="Normal map strength (%)", AutoBumpDistance="Normal map distance (yards)",
    AutoBumpTerrain="Ground and roads", AutoBumpWalls="Walls and floors", AutoBumpGrass="Grass tufts",
    AutoParallaxDepth="POM depth (%)", AutoParallaxDistance="POM distance (yards)", AutoParallaxSteps="POM trace steps",
    AutoParallaxShadows="POM self-shadowing", AutoParallaxShadowStrength="Self-shadow strength (%)", AutoParallaxShadowSteps="Self-shadow trace steps",
    AutoCavity="Cavity shading", AutoCavityStrength="Cavity strength (%)",
    Preset="Quality preset", GI="Global illumination", GIStrength="Indirect light strength (%)",
    ActorShadows="Character shadows", ActorShadowDetail="Detailed character shadows", GrassShadows="Grass shadows", ActorShadowRadius="Character shadow distance",
    ContactAO="Ambient occlusion", AOMethod="AO method", AOStrength="AO strength (%)", AORadius="AO radius (0.01 yd)",
    Weather="Enhanced rain and snow", WeatherMemory="Read native weather state", RainFog="Weather fog strength", FogClouds="Drifting fog banks", FogCloudDensity="Fog density (%)",
    HorizonHaze="Horizon haze (%)", HorizonHazeStart="Haze start (%)", HorizonHazeBand="Haze height (degrees)", HorizonHazeTerrain="Haze over terrain",
    Backend="Graphics backend", ExperimentalHDR="Enable HDR", HDRPaperWhite="White point (nits)", HDRUIWhite="UI brightness (nits)", HDRMaxNits="HDR peak brightness (nits)", HDRContrast="Contrast", HDRExposure="Exposure (EV)",
    NightBrightness="Night brightness (%)", EquipmentLights="Equipment and effect lights",
    CaptureBudgetMiB="Capture memory (MiB)", ActorShadowBudgetMiB="Character shadow memory (MiB)", MinSkinnedTriangles="Minimum character triangles", LocalLightLimit="Maximum local lights", GpuBudgetMs="GPU effect budget (ms)", ActorShadowCpuMs="Shadow CPU budget (ms)",
    CommandStream="Separate render thread", StreamFramesAhead="Buffered frames", StreamFrameSkip="Skip delayed frames", StreamOcclusionLocal="Local occlusion results", ReplayJobs="Parallel effect preparation", FrameDrawGates="Skip inactive draw work",
    PointShadowLimit="Shadow light sources", PointShadows="Local light shadows", PointShadowRefreshMs="Light shadow refresh (ms)", PointShadowFacesPerFrame="Cube faces per frame", BlobShadowStrength="Native blob shadows (%)", FarShadowInterval="Distant shadow interval (frames)", NearShadowInterval="Near shadow interval (frames)", StaticCacheSlices="Static cache update slices", ShadowDirectionSteps="Shadow direction precision", ShadowPivotCorrection="Correct shadow pivot",
    GIDistance="GI distance (yards)", GIProbeAhead="GI look-ahead (yards)", GIRays="GI rays per probe", GIBounces="GI light bounces", GIThreads="GI solver threads", GIFastBVH="Fast geometry tracing", GIProbeMoveStep="GI update movement step", GIDynamicProbes="Dynamic GI probes",
    SpellLayer="Separate spell effects", SpellLayerClasses="Spell blend types (mask)", SpellFog="Fog on spell effects", NameMask="Protect unit names", DepthFuncAware="Respect draw depth tests", terrain_radius="Terrain shadow reach (yards)",
    Diagnostics="Diagnostic logging", RenderProfile="Render profiling", DiagReplayProbe="Shadow replay probe", ShadowFateDiagnostics="Shadow lifetime diagnostics", SpellTrace="Spell trace frames", NameMaskDebug="Show name protection mask", MemMap="Address space diagnostics",
}
local function fieldLabel(f)
    if labels[f.key] then return labels[f.key] end
    local body, key = f.key:match("^(sun)_(.+)$"); if not body then body, key = f.key:match("^(moon)_(.+)$") end
    local names = {disc="disc tint",disc_gain="disc brightness",light="light tint",light_mix="light tint mix",light_strength="light strength",fog_gain="fog illumination",fog_height_scale="fog height",horizon_haze="horizon haze",glow_strength="glow strength",glow="glow tint",glow_mix="glow tint mix"}
    if body then return (body == "sun" and "Sun: " or "Moon: ") .. (names[key] or key) end
    return f.key
end
local shortLabels = {
    GIStrength="Indirect light (%)", GIDistance="GI distance (yd)", GIBounces="Light bounces",
    AOStrength="Strength (%)", AORadius="Radius (0.01 yd)",
    ActorShadowRadius="Distance (yd)", ActorShadowDetail="Detailed shadows",
    PointShadowLimit="Shadow light sources", NightBrightness="Night brightness (%)", EquipmentLights="Equipment lights",
    FogCloudDensity="Density (%)", HDRPaperWhite="White point (nits)", HDRUIWhite="UI brightness (nits)", HDRMaxNits="Peak brightness (nits)",
    AutoBumpStrength="Normal map strength (%)", AutoBumpDistance="Relief distance (yd)",
}
local function optionLabel(f, value)
    if f.key == "AutoBumpTerrain" or f.key == "AutoBumpWalls" or f.key == "AutoBumpGrass" then
        return value == "0" and "Off" or value == "1" and "Normal maps" or "Normal maps + POM"
    end
    return value == "HBAOPlus" and "HBAO+ (D3D9)" or value
end
local function rowLabel(f)
    if shortLabels[f.key] then return shortLabels[f.key] end
    if f.file == "C" then return fieldLabel(f):gsub("^[^:]+: ", "") end
    return fieldLabel(f)
end
local function blurEdit(commit)
    if not focusedEdit then return end
    local old = suppress; if not commit then suppress = true end
    focusedEdit:ClearFocus(); suppress = old
end
local function dirtyCount() local n = 0; for _ in pairs(draft) do n = n + 1 end; return n end
local dependencies = {
    AutoBumpStrength={"AutoBump"}, AutoBumpDistance={"AutoBump"}, AutoBumpTerrain={"AutoBump"}, AutoBumpWalls={"AutoBump"}, AutoBumpGrass={"AutoBump"},
    AutoParallaxDepth={"AutoBump"}, AutoParallaxDistance={"AutoBump"}, AutoParallaxSteps={"AutoBump"},
    AutoParallaxShadows={"AutoBump"}, AutoParallaxShadowStrength={"AutoBump","AutoParallaxShadows"}, AutoParallaxShadowSteps={"AutoBump","AutoParallaxShadows"},
    AutoCavity={"AutoBump"}, AutoCavityStrength={"AutoBump","AutoCavity"},
    AOMethod={"ContactAO"}, AOStrength={"ContactAO"}, AORadius={"ContactAO"},
    ActorShadowBudgetMiB={"ActorShadows"}, ActorShadowRadius={"ActorShadows"}, ActorShadowCpuMs={"ActorShadows"}, ActorShadowDetail={"ActorShadows"},
    PointShadowLimit={"PointShadows"}, PointShadowRefreshMs={"PointShadows"}, PointShadowFacesPerFrame={"PointShadows"},
    FogCloudDensity={"FogClouds"}, RainFog={"Weather"}, RenderProfile={"Diagnostics"},
    ShadowFateDiagnostics={"Diagnostics","ActorShadows"}, DiagReplayProbe={"Diagnostics","RenderProfile","ActorShadows"},
    SpellLayerClasses={"SpellLayer"}, SpellTrace={"SpellLayer"}, SpellFog={"SpellLayer"}, NameMaskDebug={"NameMask"},
    StreamFramesAhead={"CommandStream"}, StreamFrameSkip={"CommandStream"}, StreamOcclusionLocal={"CommandStream"},
}
local title = view:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOPLEFT", 16, -14); title:SetText("Northlight")
local state = view:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
state:SetPoint("TOPLEFT", 16, -37); state:SetPoint("TOPRIGHT", view, "TOPRIGHT", -16, -37); state:SetHeight(14); state:SetJustifyH("LEFT")
local stateHint = CreateFrame("Frame", nil, view); stateHint:SetAllPoints(state); stateHint:EnableMouse(true)
stateHint:SetScript("OnEnter", function(self) GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:SetText(state:GetText() or "", 1, 1, 1, nil, true); GameTooltip:Show() end)
stateHint:SetScript("OnLeave", function() GameTooltip:Hide() end)
local blocker = CreateFrame("Frame", nil, view); blocker:SetAllPoints(view); blocker:SetFrameLevel(view:GetFrameLevel() + 50); blocker:EnableMouse(true); blocker:Hide()
local sectionMenu = CreateFrame("Frame", "NorthlightRegionMenu", view, "UIDropDownMenuTemplate")
sectionMenu:SetPoint("TOPLEFT", 0, -56); UIDropDownMenu_SetWidth(sectionMenu, 290)
local scroll = CreateFrame("ScrollFrame", "NorthlightSettingsScroll", view, "UIPanelScrollFrameTemplate")
scroll:SetPoint("TOPLEFT", 16, -58); scroll:SetPoint("BOTTOMRIGHT", -32, 46)
local content = CreateFrame("Frame", nil, scroll); content:SetWidth(320); content:SetHeight(1); scroll:SetScrollChild(content)
local function button(name, parent, width, text, click)
    local b = CreateFrame("Button", name, parent, "UIPanelButtonTemplate")
    b:SetWidth(width); b:SetHeight(22); b:SetText(text); b:SetScript("OnClick", click); return b
end
local refresh = button(nil, view, 72, "Refresh", function() if not busy then blurEdit(true); refreshSnapshot() end end)
refresh:SetPoint("BOTTOMLEFT", 16, 12)
local defaults = button(nil, view, 118, "Reset section", function()
    if not snapshot or busy then return end
    blurEdit(false)
    for _, f in ipairs(snapshot.fields) do if visible(f) and f.available then draft[f.id] = "@factory" end end
    refreshRows(); updateStatus(); previewAO()
end)
defaults:SetPoint("LEFT", refresh, "RIGHT", 6, 0)
local legend = view:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall"); legend:SetPoint("BOTTOMRIGHT", -16, 17); legend:SetText("R: restart required")
visible = function(f)
    if f.file ~= file then return false end
    if file == "C" or file == "S" then return f.section == category end
    local keys = bySection[section][4]
    return not keys or (" " .. keys .. " "):find(" " .. f.key .. " ", 1, true) ~= nil
end
local function values(text) local out = {}; for s in text:gmatch("[^,]+") do out[#out + 1] = tonumber(s) or s end; return out end
local function shownValue(f, savedOnly)
    local value = (not savedOnly and (draft[f.id] or submitted[f.id])) or confirmed[f.id]
    if value == "@factory" and f.factoryOverride ~= "~" then return f.factory end
    local inherited = value == "~" or value == "@factory"
    if value == nil and f.override == "~" then
        local presetId = snapshot.fields[1].id
        inherited = (f.file == "Q" and ((not savedOnly and (draft[presetId] or submitted[presetId])) or confirmed[presetId])) or (f.file == "C" and f.section ~= "default")
    end
    if inherited then
        if f.file == "C" and f.section ~= "default" then for _, p in ipairs(snapshot.fields) do if p.file == "C" and p.section == "default" and p.key == f.key then return shownValue(p, savedOnly) end end end
        if f.file == "Q" and (f.kind ~= "enum" or f.presetOptions) then
            local preset = snapshot.fields[1]; local p = shownValue(preset, savedOnly); return tostring(values(f.presetOptions or f.options)[p == "Performance" and 3 or p == "Balanced" and 2 or 1])
        end
        return f.inherited
    end
    return value or f.value
end
local function sameValue(f, a, b)
    if a == b then return true end
    if f.kind == "check" or f.kind == "enum" then return false end
    local x, y = values(a), values(b)
    if #x ~= #y then return false end
    for i = 1, #x do if type(x[i]) ~= "number" or x[i] ~= y[i] then return false end end
    return true
end
local function change(f, value)
    if suppress or busy or presetDialog then return end
    if sameValue(f, value, shownValue(f)) then return end
    -- Compare against inheritance with this edit removed, including a pending preset/default.
    local old = draft[f.id]; draft[f.id] = nil
    local baseline = shownValue(f); draft[f.id] = old
    draft[f.id] = not sameValue(f, value, baseline) and value or nil
    updateStatus()
    if f.key == "AOMethod" or f.key == "ContactAO" or f.key == "Preset" then previewAO() end
end
local presetFrame = CreateFrame("Frame", "NorthlightPresets", content)
presetFrame:SetPoint("TOPLEFT"); presetFrame:SetHeight(310); presetFrame:Hide()
local presetTitle = presetFrame:CreateFontString(nil, "OVERLAY", "GameFontNormal"); presetTitle:SetPoint("TOPLEFT", 5, -2); presetTitle:SetText("Presets")
local presetIntro = presetFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall"); presetIntro:SetPoint("TOPLEFT", 5, -27); presetIntro:SetHeight(32); presetIntro:SetJustifyH("LEFT")
presetIntro:SetText("Choose starting settings, then adjust effects in the sections on the left.")
local presetChoices = {}
for i, p in ipairs(presets) do
    local key, label = p[1], p[2]
    local item = CreateFrame("Frame", nil, presetFrame); item:SetPoint("TOPLEFT", 5, -65 - (i - 1) * 56); item:SetHeight(54)
    item.label = item:CreateFontString(nil, "OVERLAY", "GameFontNormal"); item.label:SetPoint("TOPLEFT", 0, -5); item.label:SetText(p[2])
    item.description = item:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall"); item.description:SetPoint("TOPLEFT", 0, -23); item.description:SetHeight(26); item.description:SetJustifyH("LEFT"); item.description:SetText(p[3])
    item.select = button("NorthlightPreset_" .. key, item, 82, "Choose", function() if snapshot and not busy then blurEdit(true); requestPreset(key, label) end end)
    item.select:SetPoint("TOPRIGHT", 0, -12)
    item.line = item:CreateTexture(nil, "BACKGROUND"); item.line:SetTexture("Interface\\Buttons\\WHITE8X8"); item.line:SetVertexColor(.3, .28, .24, 1); item.line:SetHeight(1); item.line:SetPoint("BOTTOMLEFT"); item.line:SetPoint("BOTTOMRIGHT")
    presetChoices[i] = item
end
local presetWarning = presetFrame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall"); presetWarning:SetPoint("TOPLEFT", 5, -244); presetWarning:SetHeight(34); presetWarning:SetJustifyH("LEFT")
presetWarning:SetText("Choosing a preset overwrites all Northlight settings. You will be asked to confirm.")
local presetCurrent = presetFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall"); presetCurrent:SetPoint("TOPLEFT", 5, -287); presetCurrent:SetHeight(16); presetCurrent:SetJustifyH("LEFT")
StaticPopupDialogs["NORTHLIGHT_CONFIRM_PRESET"] = {
    text = "Preset: %s\n\nBy clicking Overwrite, you confirm overwriting ALL Northlight settings. Manual changes in every section will be reset.\n\nUse Apply to save the replacement.",
    button1 = "Overwrite", button2 = "Cancel", timeout = 0, whileDead = true, hideOnEscape = true, exclusive = 1, preferredIndex = 3,
    OnAccept = function(_, key)
        if busy or not snapshot or key ~= "Quality" and key ~= "Balanced" and key ~= "Performance" then return end
        blurEdit(false)
        for _, f in ipairs(snapshot.fields) do if f.available then
            draft[f.id] = f.file == "Q" and "~" or "@factory"
            if f.file == "Q" and f.key == "Preset" then draft[f.id] = key end
        end end
        refreshRows(); updateStatus(); previewAO()
    end,
    OnHide = function() presetDialog = nil; updateStatus() end,
}
requestPreset = function(key, label)
    presetDialog = StaticPopup_Show("NORTHLIGHT_CONFIRM_PRESET", label, nil, key)
    if not presetDialog then updateStatus("Close another dialog and try again.") else updateStatus() end
end
previewAO = function(reset)
    if busy or saving or not snapshot or not snapshot.aoPreview then return end
    local method, enabled
    for _, f in ipairs(snapshot.fields) do if f.file == "Q" then
        if f.key == "AOMethod" then method = shownValue(f) elseif f.key == "ContactAO" then enabled = shownValue(f) == "1" end
    end end
    if not method then return end
    local target = reset and "saved" or method .. "\t" .. (enabled and "1" or "0")
    if target == previewTarget then return end
    if refreshing then API.CancelSnapshot(); refreshing = false end
    API.CancelPreview()
    previewTarget = target
    local id, why = API.PreviewAO(reset and "saved" or method, enabled, function(ok, result)
        if not ok then previewTarget = nil; API.RecordError("AO preview", result); updateStatus(tostring(result)); return end
        if not reset then refreshSnapshot() end
    end)
    if not id then previewTarget = nil; updateStatus(why) end
end
local function dependencyReason(f)
    if not f.available then return "HDR is unavailable in this renderer build" end
    if f.file == "R" and f.key ~= "Backend" and f.key ~= "ExperimentalHDR" then
        for _, parent in ipairs(snapshot.fields) do if parent.key == "ExperimentalHDR" and shownValue(parent) == "0" then return "Enable HDR first" end end
    end
    if f.file ~= "Q" then return end
    local needs = dependencies[f.key] or (f.key:sub(1, 2) == "GI" and f.key ~= "GI" and {"GI"})
    if needs then for _, key in ipairs(needs) do for _, parent in ipairs(snapshot.fields) do
        if parent.file == "Q" and parent.key == key and shownValue(parent) == "0" then return "Requires: " .. (labels[key] or key) end
    end end end
end
local function tooltip(row)
    local f = row.field
    if not f then return end
    GameTooltip:SetOwner(row, "ANCHOR_RIGHT"); GameTooltip:SetText(fieldLabel(f)); GameTooltip:AddLine(f.key, .65, .65, .65)
    GameTooltip:AddLine(f.file .. " / [" .. f.section .. "]", 1, 1, 1)
    GameTooltip:AddLine("Saved: " .. shownValue(f, true) .. "   Active: " .. (f.available and f.active or "unavailable"), 1, 1, 1, true)
    if f.key == "AutoBump" then GameTooltip:AddLine("Generates and caches normal maps from installed textures. Directional detail is applied at full screen resolution. MPQs remain unchanged. Supports MSAA. Apply to compare on and off.", .8, .8, .8, true) end
    if f.key == "AutoBumpStrength" then GameTooltip:AddLine("Higher values make details more pronounced and can exaggerate painted shadows. Start at 75%.", .8, .8, .8, true) end
    if f.key == "AutoBumpTerrain" then GameTooltip:AddLine("Includes grass, dirt and stone painted on the ground.", .8, .8, .8, true) end
    if f.key == "AutoBumpGrass" then GameTooltip:AddLine("Applies to separate native grass tufts and respects their fade.", .8, .8, .8, true) end
    if f.key == "AutoBumpTerrain" or f.key == "AutoBumpWalls" then GameTooltip:AddLine("Off: original material. Normal maps: detailed light response. Normal maps + POM: shifts the original diffuse lookup to show depth at an angle. Uses existing textures; MPQs stay unchanged.", .8, .8, .8, true) end
    if f.key == "AutoParallaxSteps" then GameTooltip:AddLine("More steps improve grazing angles and cost more GPU time. POM fades out at its distance limit; grass tufts use normals only.", .8, .8, .8, true) end
    if f.key == "AutoParallaxShadows" then GameTooltip:AddLine("Relief blocks the dominant sunlight or moonlight. Requires Normal maps + POM for the material. More shadow steps cost more GPU time. Local lights retain their existing shadows.", .8, .8, .8, true) end
    if f.key == "AutoCavity" then GameTooltip:AddLine("Reduces ambient light inside small cracks inferred from the texture. Works with normal maps and POM; fog and direct light are preserved. Painted shadows can become too dark at high strength.", .8, .8, .8, true) end
    if f.key == "PointShadows" or f.key == "PointShadowLimit" then
        GameTooltip:AddLine("Torches, candles, lamps and compact fires cast shadows. Diffuse glows and short-lived effects do not. Works day and night where the local light is strong enough relative to sunlight.", .8, .8, .8, true)
        GameTooltip:AddLine("4 by default; adjustable from 4 to 128. Only nearby eligible sources allocate maps (about 3.5 MiB each, up to 448 MiB). More sources increase rendering cost.", 1, .82, 0, true)
    end
    if f.key == "PointShadowLimit" then GameTooltip:AddLine("Raises the local light pool when needed. For a shadow-only comparison, set Maximum local lights to 128 and GPU effect budget to 0 first; then compare 4 / 8 / 16 / 32 / 64 / 128 in the same scene.", .8, .8, .8, true) end
    if f.file == "R" and f.key ~= "Backend" then
        GameTooltip:AddLine("HDR output: " .. tostring(snapshot.hdrStatus or "unknown") .. " (" .. tostring(snapshot.hdrCode or "?") .. ")", 1, 0.85, 0.4, true)
        GameTooltip:AddLine("Requires DXVK 2/3, CommandStream, windowed mode and HDR output from the system or Wine. MSAA is supported. Restart after toggling HDR; sliders apply in game.", 0.8, 0.8, 0.8, true)
        local reasons = {
            ["command-stream-disabled"]="Enable CommandStream in Performance, then restart.",
            ["backend-not-dxvk"]="Select DXVK 2 or 3 in Renderer, then restart.",
            ["windowed-mode-required"]="Enable windowed mode.",
            ["host-scrgb-unavailable"]="The system/Wine driver does not expose HDR scRGB. Check display HDR and the Wine/Vulkan driver.",
            ["dxvk-hdr-interface-missing"]="This DXVK build lacks the D3D9 HDR interface.",
        }
        if reasons[snapshot.hdrReason] then GameTooltip:AddLine(reasons[snapshot.hdrReason], 1, .6, .3, true) end
        if f.key == "HDRContrast" then GameTooltip:AddLine("1.00 is neutral. Adjusts world contrast around middle grey; UI brightness is unchanged.", .8, .8, .8, true) end
        if f.key == "HDRExposure" then GameTooltip:AddLine("0 EV is neutral. +1 EV doubles world light; -1 EV halves it. UI brightness is unchanged.", .8, .8, .8, true) end
        if f.key == "HDRMaxNits" then GameTooltip:AddLine("Limits world HDR output. UI brightness remains independent; this does not raise SDR highlights to the chosen peak.", .8, .8, .8, true) end
    end
    GameTooltip:AddLine("Source: " .. f.origin .. (f.override == "~" and " (inherited)" or ""), 0.8, 0.8, 0.8, true)
    GameTooltip:AddLine(f.apply == "R" and "Requires a full game restart." or "Applied between frames after saving.", 1, 0.82, 0, true)
    if f.key == "AOMethod" and snapshot.aoPreview then GameTooltip:AddLine("Preview changes immediately. Apply saves the technique; Cancel restores the saved AO settings.", .8, .8, .8, true) end
    if row.disabledReason then GameTooltip:AddLine(row.disabledReason, 1, .4, .3, true) end
    GameTooltip:AddLine("Right-click to restore the inherited value.", .7, .7, .7, true)
    GameTooltip:Show()
end
local function canInherit(f)
    return f.available and not (f.file == "R" and f.key == "Backend" and not ("," .. f.options .. ","):find(",legacy,", 1, true))
end
local function numberText(f, value)
    value = math.max(f.min, math.min(f.max, math.floor(value / f.step + .5) * f.step))
    return f.step < 1 and string.format("%.2f", value) or tostring(math.floor(value))
end
local function makeRow(index)
    local row = CreateFrame("Frame", nil, content); row:EnableMouse(true)
    local function resetField()
        if not busy and not presetDialog and row.field and not dependencyReason(row.field) and canInherit(row.field) then
            blurEdit(false); draft[row.field.id] = "~"; refreshRows(); updateStatus(); previewAO()
        end
    end
    row:SetScript("OnEnter", tooltip); row:SetScript("OnLeave", function() GameTooltip:Hide() end)
    row:SetScript("OnMouseUp", function(_, mouse)
        if mouse == "RightButton" then resetField()
        elseif row.field and row.field.kind == "check" and not dependencyReason(row.field) then
            blurEdit(true); change(row.field, shownValue(row.field) == "1" and "0" or "1"); refreshRows()
        end
    end)
    row.label = row:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall"); row.label:SetHeight(14); row.label:SetJustifyH("LEFT")
    row.note = row:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall"); row.note:SetWidth(9); row.note:SetHeight(14); row.note:SetTextColor(1, .75, .35)
    row.line = row:CreateTexture(nil, "BACKGROUND"); row.line:SetTexture("Interface\\Buttons\\WHITE8X8"); row.line:SetVertexColor(.4, .37, .3, 1); row.line:SetHeight(1); row.line:SetPoint("BOTTOMLEFT"); row.line:SetPoint("BOTTOMRIGHT")
    row.reset = button(nil, row, 44, "Reset", function()
        if busy or presetDialog or not row.group then return end
        blurEdit(false)
        for _, f in ipairs(row.group.all) do if canInherit(f) then draft[f.id] = "~" end end
        refreshRows(); updateStatus(); previewAO()
    end)
    row.check = CreateFrame("CheckButton", nil, row, "UICheckButtonTemplate"); row.check:SetWidth(26); row.check:SetHeight(26)
    row.check:SetScript("OnClick", function(self) local checked = self:GetChecked(); blurEdit(true); change(row.field, checked and "1" or "0"); refreshRows() end)
    row.dropdown = CreateFrame("Frame", "NorthlightValueMenu" .. index, row, "UIDropDownMenuTemplate")
    local slider = CreateFrame("Slider", "NorthlightSlider" .. index .. "_1", row, "OptionsSliderTemplate")
    slider:SetHeight(17); slider:SetHitRectInsets(0, 0, 0, 0)
    for _, suffix in ipairs({"Low", "High", "Text"}) do _G[slider:GetName() .. suffix]:SetText(""); _G[slider:GetName() .. suffix]:Hide() end
    row.sliders = {slider}; row.edits, row.channels = {}, {}
    row.swatch = row:CreateTexture(nil, "ARTWORK"); row.swatch:SetTexture("Interface\\Buttons\\WHITE8X8"); row.swatch:SetWidth(16); row.swatch:SetHeight(16)
    for channel = 1, 3 do
        local c = channel
        local edit = CreateFrame("EditBox", "NorthlightEdit" .. index .. "_" .. c, row, "InputBoxTemplate")
        edit:SetAutoFocus(false); edit:SetMaxLetters(16); edit:SetHeight(20); edit:SetTextInsets(2, 2, 0, 0); edit:SetJustifyH("RIGHT"); edit:SetFontObject(GameFontHighlightSmall)
        edit:HookScript("OnEditFocusGained", function(self) focusedEdit = self end)
        edit:SetScript("OnTextChanged", function(_, userInput) if userInput and not suppress and not busy and not saving and not presetDialog then VideoOptionsFrameApply:Enable() end end)
        edit:SetScript("OnEditFocusLost", function(self)
            self:HighlightText(0, 0)
            if focusedEdit == self then focusedEdit = nil end
            if suppress or busy or presetDialog or not row.field or dependencyReason(row.field) then return end
            local f, value = row.field, tonumber(self:GetText())
            if not value or value ~= value then refreshRows(); updateStatus("Enter a number within the allowed range."); return end
            local text = numberText(f, value)
            if f.kind == "rgb" then local rgb = values(shownValue(f)); rgb[c] = text; text = table.concat(rgb, ",") end
            change(f, text); refreshRows()
        end)
        edit:SetScript("OnEnterPressed", function(self) self:ClearFocus() end)
        edit:SetScript("OnTabPressed", function(self) self:ClearFocus() end)
        edit:SetScript("OnEscapePressed", function() blurEdit(false); refreshRows() end)
        row.edits[c] = edit
        local label = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall"); label:SetText(({"R", "G", "B"})[c]); label:SetWidth(8); label:SetHeight(14); row.channels[c] = label
    end
    slider:SetScript("OnMouseDown", function() blurEdit(true) end)
    slider:SetScript("OnValueChanged", function(self, value)
        if suppress or busy or presetDialog or not row.field then return end
        local text = numberText(row.field, value); row.edits[1]:SetText(text); _G[self:GetName() .. "Text"]:SetText(text)
        change(row.field, text); row.label:SetText(rowLabel(row.field) .. (draft[row.field.id] and " *" or ""))
    end)
    row.more = CreateFrame("Button", nil, row); row.more:SetNormalFontObject(GameFontNormalSmall); row.more:SetHighlightFontObject(GameFontHighlightSmall); row.more:SetHeight(20)
    row.more:SetScript("OnClick", function() if not busy and not presetDialog then blurEdit(true); expanded[row.group.id] = not expanded[row.group.id]; refreshRows() end end)
    for _, widget in ipairs({row.check, slider, row.edits[1], row.edits[2], row.edits[3]}) do
        widget:SetScript("OnEnter", function() tooltip(row) end); widget:SetScript("OnLeave", function() GameTooltip:Hide() end)
        widget:SetScript("OnMouseUp", function(_, mouse) if mouse == "RightButton" then resetField() end end)
    end
    local dropdownButton = _G[row.dropdown:GetName() .. "Button"]
    if dropdownButton then dropdownButton:HookScript("OnEnter", function() tooltip(row) end); dropdownButton:HookScript("OnLeave", function() GameTooltip:Hide() end) end
    return row
end
updateStatus = function(message)
    if busy or presetDialog then blocker:Show(); CloseDropDownMenus() else blocker:Hide() end
    for _, item in ipairs(presetChoices) do if busy or presetDialog or not snapshot then item.select:Disable() else item.select:Enable() end end
    if VideoOptionsFrameCancel then if saving then VideoOptionsFrameCancel:Disable() else VideoOptionsFrameCancel:Enable() end end
    if saving then VideoOptionsFrameApply:Disable(); VideoOptionsFrameOkay:Disable() else VideoOptionsFrameOkay:Enable() end
    if busy or saving then refresh:Disable() else refresh:Enable() end
    if message then state:SetText(message); return end
    if saving then state:SetText("Northlight: saving settings…"); return end
    if busy then
        local d = NorthlightSettingsDiagnostics
        state:SetText("Northlight: loading settings" .. (d and d.pages and d.pages > 0 and (" " .. tostring(d.page) .. "/" .. tostring(d.pages)) or "…")); return
    end
    if not snapshot then state:SetText("Open this panel to load renderer settings."); return end
    local n, restart, awaiting = dirtyCount(), false, false
    for _, f in ipairs(snapshot.fields) do
        if f.available and not sameValue(f, shownValue(f), f.active) then
            if f.apply == "R" then restart = true elseif not draft[f.id] then awaiting = true end
        end
    end
    state:SetText((n > 0 and "Changed: " .. n or cached and "Cached • verifying…" or "Saved") .. (restart and " • R: restart required" or "") .. (awaiting and " • Awaiting frame" or "") .. (refreshing and n == 0 and not cached and " • Updating…" or ""))
    if n > 0 then VideoOptionsFrameApply:Enable() end
end
local function sectionGroups()
    local out, byKey, ordered, used = {}, {}, {}, {}
    if snapshot then for _, f in ipairs(snapshot.fields) do if visible(f) then byKey[f.key] = f; ordered[#ordered + 1] = f end end end
    local function take(text, target)
        for key in (text or ""):gmatch("%S+") do if byKey[key] and not used[key] then target[#target + 1] = byKey[key]; used[key] = true end end
    end
    for _, definition in ipairs(groups[section] or {}) do
        local g = {id = section .. ":" .. definition[1], title = definition[2], fields = {}, advanced = {}, all = {}}
        if definition[3] and byKey[definition[3]] then g.master = byKey[definition[3]]; used[definition[3]] = true; g.all[1] = g.master end
        take(definition[4], g.fields); take(definition[5], g.advanced)
        for _, list in ipairs({g.fields, g.advanced}) do for _, f in ipairs(list) do g.all[#g.all + 1] = f end end
        if #g.all > 0 then out[#out + 1] = g end
    end
    local rest = {id = section .. ":other", title = bySection[section][2], fields = {}, advanced = {}, all = {}}
    for _, f in ipairs(ordered) do if not used[f.key] then rest.fields[#rest.fields + 1] = f; rest.all[#rest.all + 1] = f end end
    if #rest.all > 0 then out[#out + 1] = rest end
    return out
end
refreshRows = function()
    if suppress or focusedEdit then return end
    suppress = true
    local width = math.max(180, scroll:GetWidth()); content:SetWidth(width)
    local controlWidth = math.min(156, math.floor(width * .5)); local controlX = width - controlWidth; local labelWidth = controlX - 8
    local y, index = 0, 0
    local function nextRow(kind, f, height)
        index = index + 1; local row = rows[index] or makeRow(index); rows[index] = row
        row.field, row.rowType, row.group = f, kind, nil
        row:ClearAllPoints(); row:SetPoint("TOPLEFT", 0, -y); row:SetWidth(width); row:SetHeight(height); y = y + height
        row.check:Hide(); row.dropdown:Hide(); row.reset:Hide(); row.line:Hide(); row.more:Hide(); row.note:Hide(); row.swatch:Hide(); row.label:Hide(); row.sliders[1]:Hide()
        for c = 1, 3 do row.edits[c]:Hide(); row.channels[c]:Hide() end
        row.disabledReason = f and dependencyReason(f) or nil
        row:Show(); return row
    end
    local function drawField(f)
        local row = nextRow("field", f, 28); local enabled = not row.disabledReason
        row.label:ClearAllPoints(); row.label:SetPoint("TOPLEFT", 5, -7); row.label:SetWidth(math.max(20, labelWidth - 13)); row.label:SetText(rowLabel(f) .. (draft[f.id] and " *" or "")); row.label:SetTextColor(enabled and 1 or .5, enabled and 1 or .5, enabled and 1 or .5); row.label:Show()
        row.note:ClearAllPoints(); row.note:SetPoint("TOPLEFT", labelWidth - 7, -7); row.note:SetText(f.apply == "R" and "R" or ""); row.note:Show()
        local value = shownValue(f)
        if f.kind == "check" then
            row.check:ClearAllPoints(); row.check:SetPoint("TOPLEFT", width - 26, -1); row.check:SetChecked(value == "1"); if enabled then row.check:Enable() else row.check:Disable() end; row.check:Show()
        elseif f.kind == "enum" then
            row.dropdown:ClearAllPoints(); row.dropdown:SetPoint("TOPLEFT", controlX - 16, 1); UIDropDownMenu_SetWidth(row.dropdown, controlWidth - 32)
            UIDropDownMenu_Initialize(row.dropdown, function()
                for option in f.options:gmatch("[^,]+") do local selected = option; local info = UIDropDownMenu_CreateInfo(); info.text = optionLabel(f, selected); info.checked = shownValue(f) == selected
                    info.func = function() blurEdit(true); change(f, selected); refreshRows() end; UIDropDownMenu_AddButton(info) end
            end)
            UIDropDownMenu_SetText(row.dropdown, optionLabel(f, value))
            if enabled then UIDropDownMenu_EnableDropDown(row.dropdown) else UIDropDownMenu_DisableDropDown(row.dropdown) end; row.dropdown:Show()
        else
            local v = values(value); local rgb = f.kind == "rgb"
            if not rgb then
                local slider = row.sliders[1]; slider:ClearAllPoints(); slider:SetPoint("TOPLEFT", controlX, -5); slider:SetWidth(controlWidth - 43); slider:SetMinMaxValues(f.min, f.max); slider:SetValueStep(f.step); slider:SetValue(tonumber(v[1]) or f.min)
                _G[slider:GetName() .. "Text"]:SetText(tostring(v[1])); slider:EnableMouse(enabled); slider:SetAlpha(enabled and 1 or .4); slider:Show()
            else
                row.swatch:ClearAllPoints(); row.swatch:SetPoint("TOPLEFT", controlX, -6); row.swatch:SetVertexColor(tonumber(v[1]) or 0, tonumber(v[2]) or 0, tonumber(v[3]) or 0, 1); row.swatch:Show()
            end
            local cell = math.floor((controlWidth - 20) / 3)
            for c = 1, rgb and 3 or 1 do
                local edit = row.edits[c]; edit:ClearAllPoints(); edit:SetPoint("TOPLEFT", rgb and controlX + 35 + (c - 1) * cell or width - 36, -4); edit:SetWidth(rgb and cell - 17 or 36)
                if edit ~= focusedEdit then edit:SetText(tostring(v[c] or f.min)) end
                edit:EnableMouse(enabled); edit:EnableKeyboard(enabled); edit:SetAlpha(enabled and 1 or .4); edit:Show()
                if rgb then local label = row.channels[c]; label:ClearAllPoints(); label:SetPoint("TOPLEFT", controlX + 20 + (c - 1) * cell, -7); label:Show() end
            end
        end
    end
    if section == "quality" then
        presetFrame:SetWidth(width); presetFrame:Show(); defaults:Hide()
        presetIntro:SetWidth(width - 10); presetWarning:SetWidth(width - 10); presetCurrent:SetWidth(width - 10)
        for _, item in ipairs(presetChoices) do item:SetWidth(width - 10); item.label:SetWidth(math.max(50, width - 105)); item.description:SetWidth(math.max(50, width - 105)) end
        local current = ""
        if snapshot then for _, f in ipairs(snapshot.fields) do if f.file == "Q" and f.key == "Preset" then for _, p in ipairs(presets) do if p[1] == shownValue(f) then current = p[2] end end end end end
        presetCurrent:SetText("Base preset: " .. current); y = 310
    else
        presetFrame:Hide(); defaults:Show()
        for _, g in ipairs(sectionGroups()) do
            if index > 0 then y = y + 8 end
            local header = nextRow("header", g.master, 26); header.group = g
            local x = g.master and 27 or 2
            header.label:ClearAllPoints(); header.label:SetPoint("TOPLEFT", x, -6); header.label:SetWidth(width - x - 50); header.label:SetText(g.title .. (g.master and draft[g.master.id] and " *" or "")); header.label:SetTextColor(1, .82, 0); header.label:Show(); header.line:Show()
            if g.master then header.check:ClearAllPoints(); header.check:SetPoint("TOPLEFT", 0, 0); header.check:SetChecked(shownValue(g.master) == "1"); if header.disabledReason then header.check:Disable() else header.check:Enable() end; header.check:Show() end
            local resettable = false; for _, f in ipairs(g.all) do if canInherit(f) then resettable = true end end
            header.reset:ClearAllPoints(); header.reset:SetPoint("TOPRIGHT", 0, -2); if resettable and (not g.master or g.master.available) then header.reset:Enable() else header.reset:Disable() end; header.reset:Show()
            for _, f in ipairs(g.fields) do drawField(f) end
            if #g.advanced > 0 then
                local more = nextRow("more", nil, 22); more.group = g; more.more:ClearAllPoints(); more.more:SetPoint("TOPLEFT", 2, 0); more.more:SetWidth(math.min(180, width)); more.more:SetText((expanded[g.id] and "- " or "+ ") .. "Fine tuning" .. " (" .. #g.advanced .. ")"); more.more:Show()
                if expanded[g.id] then for _, f in ipairs(g.advanced) do drawField(f) end end
            end
        end
    end
    for i = index + 1, #rows do rows[i]:Hide() end
    content:SetHeight(math.max(1, y)); suppress = false
end
local function categories()
    local out, seen = {}, {}
    if snapshot then for _, f in ipairs(snapshot.fields) do if f.file == file and not seen[f.section] then
        seen[f.section] = true; local label = f.section
        if f.section == "default" then label = "All regions (default)"
        elseif type(GetAreaInfo) == "function" then local ok, name = pcall(GetAreaInfo, tonumber(f.section:match(":(%d+)$"))); if ok and name then label = name .. " (" .. f.section .. ")" end end
        out[#out + 1] = {f.section, label}
    end end end
    table.sort(out, function(a, b) if a[1] == "default" then return true elseif b[1] == "default" then return false end; return a[1] < b[1] end)
    return out
end
local function updateMenus()
    title:SetText(section == "quality" and "Northlight" or "Northlight — " .. bySection[section][2])
    scroll:ClearAllPoints(); scroll:SetPoint("BOTTOMRIGHT", -32, 46)
    if file ~= "C" and file ~= "S" then sectionMenu:Hide(); scroll:SetPoint("TOPLEFT", 16, -58); refreshRows(); return end
    sectionMenu:Show(); scroll:SetPoint("TOPLEFT", 16, -98)
    local choices = categories(); local found = false
    for _, c in ipairs(choices) do if c[1] == category then found = true end end
    if not found then category = choices[1] and choices[1][1] or "all" end
    UIDropDownMenu_Initialize(sectionMenu, function()
        for _, c in ipairs(categories()) do local info = UIDropDownMenu_CreateInfo(); info.text = c[2]; info.checked = c[1] == category
            info.func = function() blurEdit(true); category = c[1]; UIDropDownMenu_SetText(sectionMenu, c[2]); scroll:SetVerticalScroll(0); refreshRows() end; UIDropDownMenu_AddButton(info) end
    end)
    for _, c in ipairs(choices) do if c[1] == category then UIDropDownMenu_SetText(sectionMenu, c[2]) end end
    refreshRows()
end
local function renderSnapshot()
    local ok, why = pcall(updateMenus)
    if not ok then suppress = false; API.RecordError("layout", why); updateStatus("Northlight UI: " .. tostring(why)); return false end
    updateStatus(); return true
end
refreshSnapshot = function()
    if busy or saving or refreshing then return end
    local initial = not snapshot or cached and not previewTarget
    refreshing, busy = true, not snapshot; updateStatus()
    local id, why = API.GetSnapshot(function(ok, result)
        refreshing, busy = false, false
        if ok then snapshot, confirmed, cached = result, {}, false; renderSnapshot() else updateStatus(tostring(result)) end
        if ok and initial then previewAO(true) end
    end)
    if not id then refreshing, busy = false, false; updateStatus(why) end
end
local function save(done, keepEditing)
    if busy or saving or not snapshot then return end
    if refreshing then API.CancelSnapshot(); refreshing = false end
    API.CancelPreview()
    local lines = {}; for id, value in pairs(draft) do lines[#lines + 1] = id .. "\t" .. value end; table.sort(lines)
    -- Freeze only the submitted transaction; later edits belong to the next Apply.
    submitted, draft = draft, {}
    busy, saving = not keepEditing, true; updateStatus()
    local function complete(ok, result)
        busy, saving = false, false
        if not ok then
            for key, value in pairs(submitted) do if draft[key] == nil then draft[key] = value end end
            submitted = {}; renderSnapshot(); updateStatus(tostring(result)); return
        end
        snapshot.revision = result; cached = false
        previewTarget = "saved"
        for key, value in pairs(submitted) do confirmed[key] = value end
        submitted = {}; renderSnapshot()
        -- Keep acknowledged edits visible while the authoritative status loads in the background.
        refreshSnapshot()
        if done then done() end
        if dirtyCount() > 0 then previewAO() end
        updateStatus()
    end
    local id, why = API.Commit(snapshot.revision, table.concat(lines, "\n"), complete)
    if not id then complete(false, why) end
end
panel.okay = function() end -- The native buttons wait for the asynchronous save before running other Video panels.
panel.cancel = function() if not busy and not saving then blurEdit(false); StaticPopup_Hide("NORTHLIGHT_CONFIRM_PRESET"); draft = {}; refreshRows(); updateStatus(); previewAO(true) end end
panel.default = function() if snapshot and not busy then blurEdit(false); for _, f in ipairs(snapshot.fields) do if f.available then draft[f.id] = "@factory" end end; refreshRows(); updateStatus(); previewAO() end end
panel.refresh = function() if snapshot then renderSnapshot() end end
local function showSection(owner, key)
    blurEdit(true)
    view:SetParent(owner); view:ClearAllPoints(); view:SetAllPoints(owner); view:Show()
    section, category = key, "all"; file = bySection[key][3]; scroll:SetVerticalScroll(0)
    if not snapshot and not busy then
        snapshot = API.GetCachedSnapshot()
        cached = snapshot ~= nil
        renderSnapshot()
        refreshSnapshot()
    else renderSnapshot() end
end
-- The visible content owns a poll too: skins can replace the stock window's scripts.
view:SetScript("OnUpdate", function() API.Poll(); if busy then updateStatus() end end)
panel:SetScript("OnShow", function(self) showSection(self, "quality") end)
scroll:SetScript("OnSizeChanged", function() if snapshot and not suppress then
    local ok, why = pcall(refreshRows)
    if not ok then suppress = false; API.RecordError("resize", why); updateStatus("Northlight UI: " .. tostring(why)) end
end end)
for _, b in ipairs({VideoOptionsFrameOkay, VideoOptionsFrameApply}) do
    local original = b:GetScript("OnClick")
    b:SetScript("OnClick", function(self, mouseButton)
        if busy or saving or presetDialog then return end
        blurEdit(true)
        if dirtyCount() == 0 then previewAO(true); original(self, mouseButton); return end
        local called = false
        save(function() if called then return end; called = true; if VideoOptionsFrame:IsShown() then original(self, mouseButton) end end, self == VideoOptionsFrameApply)
    end)
end
VideoOptionsFrame:HookScript("OnHide", function() if not busy then panel.cancel() end end)
OptionsFrame_AddCategory(VideoOptionsFrame, panel)
-- Native AddCategory inserts each child directly after its parent.
for i = #sections, 2, -1 do local s = sections[i]
    local child = CreateFrame("Frame", "NorthlightSection_" .. s[1], VideoOptionsFramePanelContainer)
    child.name, child.parent = s[2], "Northlight"; child:Hide()
    child.okay = function() end; child.cancel = function() end
    child.default = function() if snapshot and not busy then blurEdit(false); for _, f in ipairs(snapshot.fields) do if f.available and visible(f) then draft[f.id] = "@factory" end end; refreshRows(); updateStatus(); previewAO() end end
    child.refresh = panel.refresh
    child:SetScript("OnShow", function(self) showSection(self, s[1]) end)
    OptionsFrame_AddCategory(VideoOptionsFrame, child)
end
updateStatus()
