// The settings window's contents. See settings_panel.h; the draft rules are in settings_draft.h.
#include "settings_panel.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "imgui.h"
#include "settings_service.h"
#include "settings_draft.h"

namespace NorthlightOverlay {
namespace {
using NorthlightSettings::Field;
namespace D=NorthlightSettingsDraft;

namespace Color {
const ImVec4 Accent{0.40f,0.70f,1.00f,1.f},Edited{1.00f,0.74f,0.32f,1.f},Muted{0.60f,0.63f,0.68f,1.f},Good{0.48f,0.84f,0.54f,1.f},
    Bad{1.00f,0.48f,0.43f,1.f},Restart{1.00f,0.60f,0.26f,1.f},Heading{0.94f,0.81f,0.52f,1.f};
}

// ---- Words (from the retired Lua menu) ----
const std::map<std::string,const char*> Labels={
    {"WaveStrength","Wave strength"},{"WaveSpeed","Wave speed"},{"WaveCrestShape","Crest shape"},
    {"WaveCrestSharpness","Crest sharpness"},{"RippleStrength","Ripple strength"},
    {"FoamStrength","Foam strength"},{"OpenWaterCrestFoam","Foam on open-water crests"},
    {"ReflectionScenery","Reflections"},{"ReflectionResolution","Reflection resolution"},{"ReflectionDistance","Reflection distance"},{"ReflectionSceneryDetails","Nearby reflected scenery"},{"FineHighlights","Fine water and slime highlights"},{"Clarity","Water clarity"},{"SurfaceBlur","Depth blur"},{"Reflection","Reflection strength"},{"NearReflection","Near reflection strength"},
    {"Underwater","Underwater effects"},{"UnderwaterVisibility","Visibility (yards)"},{"UnderwaterScenery","Underwater scenery"},
    {"Palette","Whole-world appearance"},{"day_vibrance","Vibrance"},{"night_vibrance","Vibrance"},{"tone_curve_strength","Tone curve strength"},{"day_shadow_level","Shadows (25%)"},{"day_midtone_level","Midtones (50%)"},{"day_highlight_level","Highlights (75%)"},
    {"night_shadow_level","Shadows (25%)"},{"night_midtone_level","Midtones (50%)"},{"night_highlight_level","Highlights (75%)"},{"fog_tint","Fog colour"},{"fog_tint_strength","Fog tint strength"},{"shadow_contrast","Shadow contrast"},{"Slime","Slime surface"},{"Lava","Magma and fel magma surfaces"},
    {"AutoBump","Material detail"},{"AutoBumpStrength","Normal map strength (%)"},{"AutoBumpDistance","Normal map distance (yards)"},
    {"AutoBumpTerrain","Ground and roads"},{"AutoBumpWalls","Walls and floors"},{"AutoBumpGrass","Grass tufts"},
    {"AutoParallaxDepth","POM depth (%)"},{"AutoParallaxDistance","POM distance (yards)"},{"AutoParallaxSteps","POM trace steps"},
    {"AutoParallaxShadows","POM self-shadowing"},{"AutoParallaxShadowStrength","Self-shadow strength (%)"},{"AutoParallaxShadowSteps","Self-shadow trace steps"},
    {"AutoCavity","Cavity shading"},{"AutoCavityStrength","Cavity strength (%)"},
    {"Preset","Quality preset"},{"GI","Global illumination"},{"GIStrength","Indirect light strength (%)"},
    {"ActorShadows","Character shadows"},{"ActorShadowDetail","Detailed character shadows"},{"GrassShadows","Grass shadows"},{"ActorShadowRadius","Character shadow distance"},
    {"ContactAO","Ambient occlusion"},{"AOMethod","AO method"},{"AOStrength","AO strength (%)"},{"AORadius","AO radius (0.01 yd)"},
    {"Weather","Enhanced rain and snow"},{"WeatherMemory","Read native weather state"},{"RainFog","Weather fog strength"},{"RainOpacity","Rain opacity (%)"},{"FogClouds","Drifting fog banks"},{"FogCloudDensity","Fog density (%)"},
    {"HorizonHaze","Horizon haze (%)"},{"HorizonHazeStart","Haze start (%)"},{"HorizonHazeBand","Haze height (degrees)"},{"HorizonHazeTerrain","Haze over terrain"},
    {"Backend","Graphics backend"},{"ExperimentalHDR","Enable HDR"},{"HDRPaperWhite","White point (nits)"},{"HDRUIWhite","UI brightness (nits)"},{"HDRMaxNits","HDR peak brightness (nits)"},{"HDRContrast","Contrast"},{"HDRExposure","Exposure (EV)"},
    {"NightBrightness","Night brightness (%)"},{"EquipmentLights","Equipment and effect lights"},
    {"CaptureBudgetMiB","Capture memory (MiB)"},{"ActorShadowBudgetMiB","Character shadow memory (MiB)"},{"MinSkinnedTriangles","Minimum character triangles"},{"LocalLightLimit","Maximum local lights"},{"GpuBudgetMs","GPU effect budget (ms)"},{"ActorShadowCpuMs","Shadow CPU budget (ms)"},
    {"CommandStream","Separate render thread"},{"StreamFramesAhead","Buffered frames"},{"StreamFrameSkip","Skip delayed frames"},{"StreamOcclusionLocal","Local occlusion results"},{"ReplayJobs","Parallel effect preparation"},{"FrameDrawGates","Skip inactive draw work"},
    {"PointShadowLimit","Shadow light sources"},{"PointShadows","Local light shadows"},{"PointShadowRefreshMs","Light shadow refresh (ms)"},{"PointShadowFacesPerFrame","Cube faces per frame"},{"BlobShadowStrength","Native blob shadows (%)"},
    {"FarShadowInterval","Distant shadow interval (frames)"},{"NearShadowInterval","Near shadow interval (frames)"},{"StaticCacheSlices","Static cache update slices"},{"ShadowDirectionSteps","Shadow direction precision"},{"ShadowPivotCorrection","Correct shadow pivot"},
    {"GIDistance","GI distance (yards)"},{"GIProbeAhead","GI look-ahead (yards)"},{"GIRays","GI rays per probe"},{"GIBounces","GI light bounces"},{"GIThreads","GI solver threads"},{"GIFastBVH","Fast geometry tracing"},{"GIProbeMoveStep","GI update movement step"},{"GIDynamicProbes","Dynamic GI probes"},
    {"SpellLayer","Separate spell effects"},{"SpellLayerClasses","Spell blend types (mask)"},{"SpellFog","Fog on spell effects"},{"NameMask","Protect unit names"},{"DepthFuncAware","Respect draw depth tests"},{"terrain_radius","Terrain shadow reach (yards)"},
    {"Diagnostics","Diagnostic logging"},{"RenderProfile","Render profiling"},{"DiagReplayProbe","Shadow replay probe"},{"ShadowFateDiagnostics","Shadow lifetime diagnostics"},{"SpellTrace","Spell trace frames"},{"NameMaskDebug","Show name protection mask"},{"MemMap","Address space diagnostics"},
};
// Shorter row labels where the group title already gives the context.
const std::map<std::string,const char*> ShortLabels={
    {"GIStrength","Indirect light (%)"},{"GIDistance","GI distance (yd)"},{"GIBounces","Light bounces"},{"AOStrength","Strength (%)"},{"AORadius","Radius (0.01 yd)"},
    {"ActorShadowRadius","Distance (yd)"},{"ActorShadowDetail","Detailed shadows"},{"EquipmentLights","Equipment lights"},{"FogCloudDensity","Density (%)"},
    {"HDRMaxNits","Peak brightness (nits)"},{"AutoBumpDistance","Relief distance (yd)"},
};
const std::map<std::string,const char*> CelestialLabels={
    {"disc","Disc tint"},{"disc_gain","Disc brightness"},{"light","Light tint"},{"light_mix","Light tint mix"},{"light_strength","Light strength"},{"fog_gain","Fog illumination"},
    {"fog_height_scale","Fog height"},{"horizon_haze","Horizon haze"},{"glow_strength","Glow strength"},{"glow","Glow tint"},{"glow_mix","Glow tint mix"},
};
const std::map<std::string,std::vector<const char*>> Help={
    {"day_vibrance",{"Enriches muted colours in daylight. Greys and vivid colours are protected. Tone curve strength controls the whole grade."}},
    {"night_vibrance",{"Enriches muted colours at night. Greys and vivid colours are protected. Tone curve strength controls the whole grade."}},
    {"tone_curve_strength",{"Blends in the selected region's daylight and night curves. Choose Brightness, Red, Green or Blue and drag a point on its curve."}},
    {"ReflectionSceneryDetails",{"Reflects extra dock props and animated falls near water. Adds scenery preparation and draw work. Included in the Ultra Water preset."}},
    {"SurfaceBlur",{"Softens the bottom seen through the surface as the water gets deeper. Zero keeps the bottom sharp. Waves still bend the view."}},
    {"FineHighlights",{"Breaks nearby sun and moon highlights into finer ripples on water and slime. Uses extra GPU time when highlights are visible. Included in the Ultra Water preset."}},
    {"AutoBump",{"Generates and caches normal maps from installed textures. Directional detail is applied at full screen resolution. MPQs remain unchanged. Supports MSAA."}},
    {"AutoBumpStrength",{"Higher values make details more pronounced and can exaggerate painted shadows. Start at 75%."}},
    {"AutoBumpTerrain",{"Includes grass, dirt and stone painted on the ground.","Off: original material. Normal maps: detailed light response. Normal maps + POM: shifts the original diffuse lookup to show depth at an angle."}},
    {"AutoBumpWalls",{"Off: original material. Normal maps: detailed light response. Normal maps + POM: shifts the original diffuse lookup to show depth at an angle."}},
    {"AutoBumpGrass",{"Applies to separate native grass tufts and respects their fade."}},
    {"AutoParallaxSteps",{"More steps improve grazing angles and cost more GPU time. POM fades out at its distance limit; grass tufts use normals only."}},
    {"AutoParallaxShadows",{"Relief blocks the dominant sunlight or moonlight. Requires Normal maps + POM for the material. More shadow steps cost more GPU time. Local lights retain their existing shadows."}},
    {"AutoCavity",{"Reduces ambient light inside small cracks inferred from the texture. Works with normal maps and POM; fog and direct light are preserved. Painted shadows can become too dark at high strength."}},
    {"PointShadows",{"Torches, candles, lamps and compact fires cast shadows. Diffuse glows and short-lived effects do not.","Only nearby eligible sources allocate maps (about 3.5 MiB each). More sources increase rendering cost."}},
    {"PointShadowLimit",{"4 by default; adjustable from 4 to 128. Only nearby eligible sources allocate maps (about 3.5 MiB each, up to 448 MiB).",
        "For a shadow-only comparison, set Maximum local lights to 128 and GPU effect budget to 0 first; then compare 4 / 8 / 16 / 32 / 64 / 128 in the same scene."}},
    {"HDRContrast",{"1.00 is neutral. Adjusts world contrast around middle grey; UI brightness is unchanged."}},
    {"HDRExposure",{"0 EV is neutral. +1 EV doubles world light; -1 EV halves it. UI brightness is unchanged."}},
    {"HDRMaxNits",{"Limits world HDR output. UI brightness remains independent; this does not raise SDR highlights to the chosen peak."}},
    {"ExperimentalHDR",{"Requires DXVK 2 or 3, the separate render thread, windowed mode and HDR output from the system or Wine. MSAA is supported."}},
    {"Backend",{"Which Direct3D 9 implementation the renderer runs on. Takes effect after a game restart."}},
    {"terrain_radius",{"How far terrain casts shadows in this region. Larger values cost more GPU time."}},
};

struct Group { const char* id; const char* title; const char* master; const char* fields; const char* advanced; };
struct Section { const char* id; const char* title; const char* keys; std::vector<Group> groups; bool open; };
const std::vector<Section> QualitySections={
    {"lighting","Lighting & AO","GI GIStrength GIRays GIBounces GIProbeMoveStep GIDynamicProbes GIThreads GIFastBVH GIDistance GIProbeAhead NightBrightness ContactAO AOMethod AOStrength AORadius EquipmentLights",{
        {"gi","Global illumination","GI","GIStrength GIDistance GIBounces","GIRays GIProbeMoveStep GIDynamicProbes GIThreads GIFastBVH GIProbeAhead"},
        {"ao","Ambient occlusion","ContactAO","AOMethod AOStrength AORadius",""},
        {"ambient","Environment",nullptr,"NightBrightness EquipmentLights",""}},true},
    {"materials","Materials & parallax","AutoBump AutoBumpTerrain AutoBumpWalls AutoBumpGrass AutoBumpStrength AutoBumpDistance AutoParallaxDepth AutoParallaxDistance AutoParallaxSteps AutoParallaxShadows AutoParallaxShadowStrength AutoParallaxShadowSteps AutoCavity AutoCavityStrength",{
        {"bump","Material modes","AutoBump","AutoBumpTerrain AutoBumpWalls AutoBumpGrass AutoBumpStrength AutoBumpDistance",""},
        {"parallax","POM",nullptr,"AutoParallaxDepth AutoParallaxDistance AutoParallaxSteps",""},
        {"pomshadow","POM self-shadowing","AutoParallaxShadows","AutoParallaxShadowStrength AutoParallaxShadowSteps",""},
        {"cavity","Cavity shading","AutoCavity","AutoCavityStrength",""}},false},
    {"shadows","Shadows","ActorShadows ActorShadowDetail GrassShadows ActorShadowRadius BlobShadowStrength FarShadowInterval NearShadowInterval PointShadows PointShadowLimit PointShadowRefreshMs PointShadowFacesPerFrame ShadowDirectionSteps StaticCacheSlices ShadowPivotCorrection",{
        {"actors","Character shadows","ActorShadows","ActorShadowRadius ActorShadowDetail GrassShadows","ActorShadowBudgetMiB ActorShadowCpuMs"},
        {"point","Local light shadows","PointShadows","PointShadowLimit","PointShadowRefreshMs PointShadowFacesPerFrame"},
        {"cache","Shadow updates",nullptr,"BlobShadowStrength","FarShadowInterval NearShadowInterval ShadowDirectionSteps StaticCacheSlices ShadowPivotCorrection"}},false},
    {"atmosphere","Atmosphere & weather","Weather WeatherMemory RainFog RainOpacity FogClouds FogCloudDensity HorizonHaze HorizonHazeStart HorizonHazeBand HorizonHazeTerrain",{
        {"fog","Fog banks","FogClouds","FogCloudDensity",""},
        {"weather","Rain and snow","Weather","RainFog RainOpacity","WeatherMemory"},
        {"haze","Horizon haze",nullptr,"HorizonHaze HorizonHazeStart HorizonHazeTerrain","HorizonHazeBand"}},false},
    {"spells","Spells & names","SpellLayer SpellLayerClasses SpellFog NameMask DepthFuncAware",{
        {"spells","Spell effects","SpellLayer","SpellFog","SpellLayerClasses DepthFuncAware"},
        {"names","Unit name protection","NameMask","",""}},false},
    {"performance","Performance","CommandStream StreamFramesAhead StreamFrameSkip StreamOcclusionLocal ReplayJobs FrameDrawGates MinSkinnedTriangles CaptureBudgetMiB ActorShadowBudgetMiB LocalLightLimit GpuBudgetMs ActorShadowCpuMs",{
        {"stream","Render thread","CommandStream","StreamFramesAhead ReplayJobs FrameDrawGates","StreamFrameSkip StreamOcclusionLocal"},
        {"budgets","Resource budgets",nullptr,"GpuBudgetMs ActorShadowCpuMs LocalLightLimit","MinSkinnedTriangles CaptureBudgetMiB ActorShadowBudgetMiB"}},false},
    {"diagnostics","Diagnostics","Diagnostics RenderProfile DiagReplayProbe ShadowFateDiagnostics SpellTrace NameMaskDebug MemMap",{
        {"logging","Diagnostics","Diagnostics","RenderProfile","DiagReplayProbe ShadowFateDiagnostics"},
        {"debug","Additional diagnostics",nullptr,"SpellTrace NameMaskDebug MemMap",""}},false},
};
const std::vector<Group> HdrGroups={{"hdr","HDR output","ExperimentalHDR","HDRPaperWhite HDRUIWhite HDRMaxNits HDRContrast HDRExposure",""}};
const std::vector<Group> CelestialGroups={
    {"sun","Sun",nullptr,"sun_disc sun_disc_gain sun_light sun_light_strength","sun_light_mix sun_fog_gain sun_fog_height_scale sun_horizon_haze sun_glow_strength sun_glow sun_glow_mix"},
    {"moon","Moon",nullptr,"moon_disc moon_disc_gain moon_light moon_light_strength","moon_light_mix moon_fog_gain moon_fog_height_scale moon_horizon_haze moon_glow_strength moon_glow moon_glow_mix"},
};
std::vector<std::string> words(const char* text);
// The old menu's preset page.
struct Preset { const char* key; const char* text; };
const Preset Presets[3]={{"Quality","Maximum effect detail."},{"Balanced","Quality with a moderate effect budget."},{"Performance","Less work spent on effects."}};
const char* const QuickKeys="GI ActorShadows ContactAO Weather FogClouds";   // the old menu's Quality page toggles
// Sidebar pages, in order. quality: the QualitySections id a Quality page draws.
enum class Page { Overview, Lighting, Materials, Shadows, Atmosphere, Water, Celestial, ShadowRange, Spells, Renderer, Performance, Diagnostics, Count };
struct PageInfo { Page page; const char* title; const char* quality; };
const PageInfo Pages[]={
    {Page::Overview,"Overview",nullptr},{Page::Lighting,"Lighting & AO","lighting"},{Page::Materials,"Materials & parallax","materials"},
    {Page::Shadows,"Shadows","shadows"},{Page::Atmosphere,"Atmosphere & weather","atmosphere"},{Page::Water,"Water",nullptr},{Page::Celestial,"Sun & moon",nullptr},
    {Page::ShadowRange,"Shadow range",nullptr},{Page::Spells,"Spells & names","spells"},{Page::Renderer,"Renderer & HDR",nullptr},
    {Page::Performance,"Performance","performance"},{Page::Diagnostics,"Diagnostics","diagnostics"},
};
static_assert(sizeof(Pages)/sizeof(Pages[0])==std::size_t(Page::Count),"one entry per page");
const Section* qualitySection(const char* id){if(id)for(const auto& s:QualitySections)if(std::string(s.id)==id)return &s;return nullptr;}
// Every key a Quality section names (its key list and its groups).
std::vector<std::string> sectionKeys(const Section& s){
    std::vector<std::string> out=words(s.keys);
    for(const auto& g:s.groups)for(const char* list:{g.master,g.fields,g.advanced})for(auto& k:words(list))if(std::find(out.begin(),out.end(),k)==out.end())out.push_back(k);
    return out;
}
// Exact menu keys and names from the active enUS AreaTable.dbc; only snapshot-backed profiles are listed.
const std::map<std::string,const char*> Zones={
    {"Azeroth:36", "Alterac Mountains"},
    {"Azeroth:45", "Arathi Highlands"},
    {"Azeroth:3", "Badlands"},
    {"Azeroth:25", "Blackrock Mountain"},
    {"Azeroth:4", "Blasted Lands"},
    {"Azeroth:85/159", "Brill"},
    {"Azeroth:46", "Burning Steppes"},
    {"Azeroth:3459", "City"},
    {"Azeroth:41", "Deadwind Pass"},
    {"Azeroth:2280", "DO NOT USE"},
    {"Azeroth:269", "Dun Algaz"},
    {"Azeroth:11/836", "Dun Algaz, Wetlands"},
    {"Azeroth:1", "Dun Morogh"},
    {"Azeroth:10", "Duskwood"},
    {"Azeroth:139", "Eastern Plaguelands"},
    {"Azeroth:12", "Elwynn Forest"},
    {"Azeroth:408", "Gillijim's Isle"},
    {"Azeroth:267", "Hillsbrad Foothills"},
    {"Azeroth:1537", "Ironforge"},
    {"Azeroth:409", "Island of Doctor Lapidis"},
    {"Azeroth:21", "Kul Tiras"},
    {"Azeroth:38", "Loch Modan"},
    {"Azeroth:170", "Lordamere Lake"},
    {"Azeroth:30", "Nine"},
    {"Azeroth:2037", "Quel'thalas"},
    {"Azeroth:44", "Redridge Mountains"},
    {"Azeroth:51", "Searing Gorge"},
    {"Azeroth:130", "Silverpine Forest"},
    {"Azeroth:296", "South Seas UNUSED"},
    {"Azeroth:1519", "Stormwind City"},
    {"Azeroth:33", "Stranglethorn Vale"},
    {"Azeroth:8", "Swamp of Sorrows"},
    {"Azeroth:330", "Thandol Span"},
    {"Azeroth:45/880", "Thandol Span, Arathi Highlands"},
    {"Azeroth:308", "The Forbidding Sea"},
    {"Azeroth:214", "The Great Sea"},
    {"Azeroth:47", "The Hinterlands"},
    {"Azeroth:293", "Thoradin's Wall"},
    {"Azeroth:267/1057", "Thoradin's Wall, Hillsbrad Foothills"},
    {"Azeroth:85", "Tirisfal Glades"},
    {"Azeroth:1497", "Undercity"},
    {"Azeroth:1497@inside", "Undercity interior"},
    {"Azeroth:276", "UNUSED Stonewrought Pass"},
    {"Azeroth:1579", "Unused The Deadmines 002"},
    {"Azeroth:28", "Western Plaguelands"},
    {"Azeroth:40", "Westfall"},
    {"Azeroth:11", "Wetlands"},
    {"Expansion01:3917", "Auchindoun"},
    {"Expansion01:3524", "Azuremyst Isle"},
    {"Expansion01:3522", "Blade's Edge Mountains"},
    {"Expansion01:3525", "Bloodmyst Isle"},
    {"Expansion01:3430", "Eversong Woods"},
    {"Expansion01:3430/3665", "Falconwing Square"},
    {"Expansion01:3430/3462", "Fairbreeze Village"},
    {"Expansion01:3433", "Ghostlands"},
    {"Expansion01:3483", "Hellfire Peninsula"},
    {"Expansion01:4080", "Isle of Quel'Danas"},
    {"Expansion01:3518", "Nagrand"},
    {"Expansion01:3523", "Netherstorm"},
    {"Expansion01:3520", "Shadowmoon Valley"},
    {"Expansion01:3703", "Shattrath City"},
    {"Expansion01:3487", "Silvermoon City"},
    {"Expansion01:3519", "Terokkar Forest"},
    {"Expansion01:3557", "The Exodar"},
    {"Expansion01:3455", "The North Sea"},
    {"Expansion01:3479", "The Veiled Sea"},
    {"Expansion01:3540", "Twisting Nether"},
    {"Expansion01:3521", "Zangarmarsh"},
    {"Kalimdor:331", "Ashenvale"},
    {"Kalimdor:148/442", "Auberdine"},
    {"Kalimdor:16", "Azshara"},
    {"Kalimdor:1941", "Caverns of Time"},
    {"Kalimdor:148", "Darkshore"},
    {"Kalimdor:1657", "Darnassus"},
    {"Kalimdor:405", "Desolace"},
    {"Kalimdor:14", "Durotar"},
    {"Kalimdor:15", "Dustwallow Marsh"},
    {"Kalimdor:361", "Felwood"},
    {"Kalimdor:357", "Feralas"},
    {"Kalimdor:3478", "Gates of Ahn'Qiraj"},
    {"Kalimdor:876", "GM Island"},
    {"Kalimdor:2917", "Hall of Legends"},
    {"Kalimdor:616", "Hyjal"},
    {"Kalimdor:493", "Moonglade"},
    {"Kalimdor:215", "Mulgore"},
    {"Kalimdor:2159", "Onyxia's Lair"},
    {"Kalimdor:1637", "Orgrimmar"},
    {"Kalimdor:1377", "Silithus"},
    {"Kalimdor:377", "Southfury River"},
    {"Kalimdor:406", "Stonetalon Mountains"},
    {"Kalimdor:440", "Tanaris"},
    {"Kalimdor:141", "Teldrassil"},
    {"Kalimdor:17", "The Barrens"},
    {"Kalimdor:332", "The Great Sea"},
    {"Kalimdor:457", "The Veiled Sea"},
    {"Kalimdor:400", "Thousand Needles"},
    {"Kalimdor:1638", "Thunder Bluff"},
    {"Kalimdor:490", "Un'Goro Crater"},
    {"Kalimdor:618", "Winterspring"},
    {"Northrend:3477", "Azjol-Nerub"},
    {"Northrend:3537", "Borean Tundra"},
    {"Northrend:2817", "Crystalsong Forest"},
    {"Northrend:495/3980", "Daggercap Bay"},
    {"Northrend:4395", "Dalaran"},
    {"Northrend:65", "Dragonblight"},
    {"Northrend:4602", "Force Interior"},
    {"Northrend:394", "Grizzly Hills"},
    {"Northrend:495", "Howling Fjord"},
    {"Northrend:4742", "Hrothgar's Landing"},
    {"Northrend:210", "Icecrown"},
    {"Northrend:3711", "Sholazar Basin"},
    {"Northrend:3979", "The Frozen Sea"},
    {"Northrend:4258", "The North Sea"},
    {"Northrend:4630", "The North Sea"},
    {"Northrend:67", "The Storm Peaks"},
    {"Northrend:4201", "Vordrassil's Tears"},
    {"Northrend:394/4202", "Vordrassil's Tears, Grizzly Hills"},
    {"Northrend:4197", "Wintergrasp"},
    {"Northrend:66", "Zul'Drak"},
};
// Map.dbc directory names remain the keys; localized display names label map-wide fallback sections.
const std::map<std::string,const char*> MapLabels={
    {"AhnQiraj", "Ruins of Ahn'Qiraj"},
    {"AhnQirajTemple", "Ahn'Qiraj Temple"},
    {"ArgentTournamentDungeon", "Trial of the Champion"},
    {"ArgentTournamentRaid", "Trial of the Crusader"},
    {"AuchindounDemon", "Auchindoun: Sethekk Halls"},
    {"AuchindounDraenei", "Auchindoun: Auchenai Crypts"},
    {"AuchindounEthereal", "Auchindoun: Mana-Tombs"},
    {"AuchindounShadow", "Auchindoun: Shadow Labyrinth"},
    {"Azeroth", "Eastern Kingdoms"},
    {"Azjol_LowerCity", "Ahn'kahet: The Old Kingdom"},
    {"Azjol_Uppercity", "Azjol-Nerub"},
    {"Blackfathom", "Blackfathom Deeps"},
    {"BlackrockDepths", "Blackrock Depths"},
    {"BlackRockSpire", "Blackrock Spire"},
    {"BlackTemple", "Black Temple"},
    {"BlackwingLair", "Blackwing Lair"},
    {"bladesedgearena", "Blade's Edge Arena"},
    {"CavernsOfTime", "Opening of the Dark Portal"},
    {"ChamberOfAspectsBlack", "The Obsidian Sanctum"},
    {"ChamberofAspectsRed", "The Ruby Sanctum"},
    {"CoilfangDraenei", "Coilfang: The Slave Pens"},
    {"CoilfangMarsh", "Coilfang: The Underbog"},
    {"CoilfangPumping", "Coilfang: The Steamvault"},
    {"CoilfangRaid", "Coilfang: Serpentshrine Cavern"},
    {"DalaranArena", "Dalaran Sewers"},
    {"DalaranPrison", "Violet Hold"},
    {"DeadminesInstance", "Deadmines"},
    {"DeathKnightStart", "Ebon Hold"},
    {"DeeprunTram", "Deeprun Tram"},
    {"DireMaul", "Dire Maul"},
    {"DrakTheronKeep", "Drak'Tharon Keep"},
    {"Expansion01", "Outland"},
    {"GnomeragonInstance", "Gnomeregan"},
    {"GruulsLair", "Gruul's Lair"},
    {"GunDrak", "Gundrak"},
    {"HallsOfReflection", "Halls of Reflection"},
    {"HellfireDemon", "Hellfire Citadel: The Blood Furnace"},
    {"HellfireMilitary", "Hellfire Citadel: The Shattered Halls"},
    {"HellfireRaid", "Magtheridon's Lair"},
    {"HellfireRampart", "Hellfire Citadel: Ramparts"},
    {"HillsbradPast", "The Escape From Durnholde"},
    {"HyjalPast", "The Battle for Mount Hyjal"},
    {"IcecrownCitadel", "Icecrown Citadel"},
    {"IcecrownCitadel5Man", "The Forge of Souls"},
    {"IsleofConquest", "Isle of Conquest"},
    {"Kalimdor", "Kalimdor"},
    {"Karazahn", "Karazhan"},
    {"Mauradon", "Maraudon"},
    {"MoltenCore", "Molten Core"},
    {"MonasteryInstances", "Scarlet Monastery"},
    {"NetherstormBG", "Eye of the Storm"},
    {"Nexus70", "The Nexus"},
    {"Nexus80", "The Oculus"},
    {"NexusRaid", "The Eye of Eternity"},
    {"Northrend", "Northrend"},
    {"NorthrendBG", "Strand of the Ancients"},
    {"OnyxiaLairInstance", "Onyxia's Lair"},
    {"OrgrimmarArena", "The Ring of Valor"},
    {"OrgrimmarInstance", "Ragefire Chasm"},
    {"PVPLordaeron", "Ruins of Lordaeron"},
    {"PVPZone01", "Alterac Valley"},
    {"PVPZone03", "Warsong Gulch"},
    {"PVPZone04", "Arathi Basin"},
    {"PVPZone05", "Nagrand Arena"},
    {"QuarryofTears", "Pit of Saron"},
    {"RazorfenDowns", "Razorfen Downs"},
    {"RazorfenKraulInstance", "Razorfen Kraul"},
    {"SchoolofNecromancy", "Scholomance"},
    {"Shadowfang", "Shadowfang Keep"},
    {"StormwindJail", "Stormwind Stockade"},
    {"Stratholme", "Stratholme"},
    {"Stratholme Raid", "Naxxramas"},
    {"StratholmeCOT", "The Culling of Stratholme"},
    {"SunkenTemple", "Sunken Temple"},
    {"Sunwell5ManFix", "Magister's Terrace"},
    {"SunwellPlateau", "The Sunwell"},
    {"TanarisInstance", "Zul'Farrak"},
    {"TempestKeepArcane", "Tempest Keep: The Arcatraz"},
    {"TempestKeepAtrium", "Tempest Keep: The Botanica"},
    {"TempestKeepFactory", "Tempest Keep: The Mechanar"},
    {"TempestKeepRaid", "Tempest Keep"},
    {"Uldaman", "Uldaman"},
    {"Ulduar70", "Halls of Stone"},
    {"Ulduar80", "Halls of Lightning"},
    {"UlduarRaid", "Ulduar"},
    {"UtgardePinnacle", "Utgarde Pinnacle"},
    {"Valgarde70", "Utgarde Keep"},
    {"WailingCaverns", "Wailing Caverns"},
    {"WintergraspRaid", "Vault of Archavon"},
    {"Zul'gurub", "Zul'Gurub"},
    {"ZulAman", "Zul'Aman"},
};

const char* find(const std::map<std::string,const char*>& m,const std::string& key){auto i=m.find(key);return i==m.end()?nullptr:i->second;}
std::string celestialName(const std::string& key,std::string* body){
    for(const char* prefix:{"sun_","moon_"}){const std::string p=prefix;
        if(key.compare(0,p.size(),p)==0){if(body)*body=p=="sun_"?"Sun":"Moon";const char* name=find(CelestialLabels,key.substr(p.size()));return name?name:key.substr(p.size());}}
    return {};
}
bool mapDefaultSection(const std::string& section){
    return section.size()>8&&section.compare(section.size()-8,8,":default")==0;
}
std::string mapDirectory(const std::string& section){
    return mapDefaultSection(section)?section.substr(0,section.size()-8):std::string();
}
std::string mapGroup(const std::string& map){
    if(map=="Azeroth")return "Eastern Kingdoms";
    if(map=="Kalimdor")return "Kalimdor";
    if(map=="Expansion01")return "Outland";
    if(map=="Northrend")return "Northrend";
    return {};
}
// Expansion01 also contains these verified Azeroth locations; unlisted area roots stay grouped as Outland.
const std::map<std::string,const char*> ExpansionAreaGroups={
    {"3430","Eastern Kingdoms"},{"3433","Eastern Kingdoms"},{"3455","Eastern Kingdoms"},
    {"3487","Eastern Kingdoms"},{"4080","Eastern Kingdoms"},
    {"3479","Kalimdor"},{"3524","Kalimdor"},{"3525","Kalimdor"},{"3557","Kalimdor"},
};
std::string expansionAreaGroup(const std::string& section){
    const auto colon=section.find(':');
    auto area=section.substr(colon+1);
    if(const auto suffix=area.find_first_of("/@");suffix!=area.npos)area.resize(suffix);
    if(const char* group=find(ExpansionAreaGroups,area))return group;
    return "Outland";
}
std::string regionGroup(const std::string& section){
    if(section=="default")return "All regions";
    const auto colon=section.find(':');
    if(colon==section.npos)return "Other profiles";
    const auto map=section.substr(0,colon);
    if(mapDefaultSection(section)){
        if(const auto group=mapGroup(map);!group.empty())return group+" map defaults";
        return "Instance map defaults";
    }
    if(map=="Expansion01")return expansionAreaGroup(section);
    if(const auto group=mapGroup(map);!group.empty())return group;
    if(find(MapLabels,map))return "Instance regions";
    return "Other profiles";
}
int regionGroupOrder(const std::string& group){
    if(group=="All regions")return 0;
    if(group=="Eastern Kingdoms")return 1;
    if(group=="Kalimdor")return 2;
    if(group=="Outland")return 3;
    if(group=="Northrend")return 4;
    if(group=="Eastern Kingdoms map defaults")return 5;
    if(group=="Kalimdor map defaults")return 6;
    if(group=="Outland map defaults")return 7;
    if(group=="Northrend map defaults")return 8;
    if(group=="Instance regions")return 9;
    if(group=="Instance map defaults")return 10;
    return 11;
}
std::string folded(std::string text){
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});
    return text;
}
std::string fieldLabel(const Field& f){
    if(f.file==NorthlightSettings::WaterHostFile)return "Use Tideglass";
    if(const char* l=find(Labels,f.key))return l;
    std::string body;const auto name=celestialName(f.key,&body);if(!name.empty())return body+": "+name;
    return f.key;
}
std::string rowLabel(const Field& f){
    if(const char* l=find(ShortLabels,f.key))return l;
    if(f.file==D::Celestial){const auto name=celestialName(f.key,nullptr);if(!name.empty())return name;}
    return fieldLabel(f);
}
std::string regionLabel(const std::string& section){
    if(section=="default")return "All regions (default)";
    if(mapDefaultSection(section)){
        const auto map=mapDirectory(section);const char* name=find(MapLabels,map);
        return std::string(name?name:map.c_str())+" (map default; "+section+")";
    }
    if(const char* zone=find(Zones,section))return std::string(zone)+"  ("+section+")";
    return section;
}
std::string optionLabel(const Field& f,const std::string& value){
    if(D::modeField(f))return value=="0"?"Off":value=="1"?"Normal maps":"Normal maps + POM";
    if(f.file==NorthlightSettings::WaterFile){
        if(f.key=="ReflectionScenery" && value=="0")return "Sky only";
        if(f.key=="ReflectionScenery" && value=="1")return "Scenery + sky";
        if(f.key=="ReflectionDistance"){
            if(value=="0")return "Standard";
            if(value=="1")return "Short";
            if(value=="2")return "Far";
        }
        if(f.key=="ReflectionResolution")return value+"%";
    }
    if(value=="HBAOPlus")return "HBAO+ (D3D9)";
    if(f.file==D::Renderer&&f.key=="Backend"){
        if(value=="dxvk")return "DXVK 3";
        if(value=="dxvk2")return "DXVK 2";
        if(value=="native")return "System Direct3D 9";
        if(value=="legacy")return "Existing d3d9.dll (legacy)";
    }
    return value;
}
const char* fileName(unsigned file){
    return file<NorthlightSettings::FileCount?NorthlightSettings::FileNames[file]:"?";
}
const char* originText(const std::string& origin){
    if(origin=="f"||origin=="override")return "set in the file";
    if(origin=="l")return "legacy shadow-experiment.ini";
    if(origin=="p")return "preset";
    if(origin=="d")return "default";
    if(origin=="inherited")return "inherited";
    return origin.c_str();
}
std::vector<std::string> words(const char* text){
    std::vector<std::string> out;if(!text)return out;std::string w;
    for(const char* c=text;;++c){if(*c==' '||*c==0){if(!w.empty())out.push_back(w);w.clear();if(!*c)break;}else w+=*c;}
    return out;
}
double number(const std::string& s,double fallback=0){char* end=nullptr;const double v=std::strtod(s.c_str(),&end);return end!=s.c_str()&&std::isfinite(v)?v:fallback;}
bool integral(const Field& f){return number(f.step,1)>=1&&std::floor(number(f.low))==number(f.low)&&std::floor(number(f.high))==number(f.high);}
int decimals(const Field& f){const double step=number(f.step,1);if(step>=1)return 0;return std::clamp(int(std::ceil(-std::log10(step)-1e-9)),0,4);}
double quantize(const Field& f,double v){
    const double lo=number(f.low),hi=number(f.high),step=number(f.step,0);
    if(step>0)v=lo+std::round((v-lo)/step)*step;
    return std::clamp(v,std::min(lo,hi),std::max(lo,hi));
}
std::string text(const Field& f,double v){char b[64];std::snprintf(b,sizeof b,"%.*f",decimals(f),quantize(f,v));return b;}
ImU32 u32(const ImVec4& c){return ImGui::ColorConvertFloat4ToU32(c);}
// Right-aligns the next item of the given width on the current line.
void alignRight(float width){const float x=ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x-width;ImGui::SameLine(0,0);ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),x));}
float buttonWidth(const char* label){return ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2;}
} // namespace

struct SettingsPanel::State {
    using Service=NorthlightSettings::Service;
    enum class Confirm { None, Preset, Factory };
    std::shared_ptr<Service> service;
    std::string hotkey;
    std::shared_ptr<const NorthlightSettings::Snapshot> snapshot;
    NorthlightSettings::Status status;
    D::Draft draft;
    std::map<std::string,std::array<float,3>> live;   // a slider or colour being dragged: committed on release
    bool wantSnapshot=false,wantPreview=false,wantClear=false,previewing=false,saving=false,awaitingSnapshot=false,savingAppearancePreset=false;
    std::uint64_t commitSerial=0,appearancePresetSerial=0;
    char appearancePresetName[64]{};bool appearancePresetModal=false;
    ImVec4 noteColor=Color::Bad;
    std::string region="default",note;                 // note: a local message (a refused request), cleared by the next action
    std::string waterRegion="default";
    struct RegionChoice { std::string section,label,group,search; };
    const NorthlightSettings::Snapshot* regionChoicesSnapshot=nullptr;
    std::vector<RegionChoice> regionChoices,waterRegionChoices;
    std::array<char,128> regionFilter{},waterRegionFilter{};
    mutable const NorthlightSettings::Snapshot* pageFieldsSnapshot=nullptr;
    mutable std::array<std::vector<const Field*>,std::size_t(Page::Count)> pageFieldCache;
    int curveChannel=0,selectedPoint=0,dragPoint=-1;
    ImVec2 curveDragOffset{};
    bool curveDragged=false;
    double nextCurvePreview=0;
    bool compareRequested=false,comparing=false;
    Page page=Page::Overview;                             // kept for the session
    std::string presetChoice;bool presetCelestial=true,presetRange=true;   // the preset dialog
    Confirm confirm=Confirm::None;
    float scale=1;

    bool locked()const{return saving||awaitingSnapshot||savingAppearancePreset||!snapshot;}
    // ---- Service ----
    void poll(){
        if(!service)return;
        if(wantSnapshot&&service->requestSnapshot())wantSnapshot=false;
        if(auto latest=service->snapshot();latest&&latest!=snapshot){
            snapshot=latest;draft.bind(&snapshot->fields);
            if(awaitingSnapshot){draft.clear();live.clear();awaitingSnapshot=false;}
            if(region!="default"&&!draft.field(D::Celestial,region,"sun_disc"))region="default";
        }
        status=service->status();
        if(saving&&status.serial>commitSerial&&status.state!=NorthlightSettings::Status::State::Busy){
            saving=false;
            if(status.state==NorthlightSettings::Status::State::Saved){awaitingSnapshot=true;wantSnapshot=true;previewing=false;wantPreview=false;}
        }
        if(savingAppearancePreset&&status.serial>appearancePresetSerial&&status.state!=NorthlightSettings::Status::State::Busy){
            savingAppearancePreset=false;note=status.message;
            noteColor=status.state==NorthlightSettings::Status::State::Saved?Color::Good:status.state==NorthlightSettings::Status::State::Error?Color::Bad:Color::Muted;
        }
        if(wantClear&&!saving&&service->clearPreview()){wantClear=false;previewing=false;}
        if(wantPreview&&!saving&&!wantClear){
            auto pending=changes();
            if(comparing)if(const Field* strength=draft.field(D::Celestial,region,"tone_curve_strength")){
                const auto id=strength->id();
                pending.erase(std::remove_if(pending.begin(),pending.end(),[&](const auto& c){return c.id==id;}),pending.end());
                pending.push_back({id,"0"});
            }
            if(service->preview(std::move(pending))){wantPreview=false;previewing=comparing||!draft.empty();}
        }
    }
    std::vector<NorthlightSettings::Change> changes()const{
        std::vector<NorthlightSettings::Change> out;for(auto& c:draft.changes())out.push_back({c.first,c.second});return out;
    }
    void edited(const Field& f){note.clear();if(f.apply=="F")wantPreview=true;}
    void set(const Field& f,const std::string& value){
        if(f.section=="RegionalSlime"||f.key=="fog_tint_strength"||f.key=="shadow_contrast"||f.key=="tone_curve_strength")draft.pin(f,value);
        else draft.set(f,value);
        edited(f);
    }
    void inherit(const Field& f){if(!D::canInherit(f))return;draft.inherit(f);live.erase(f.id());edited(f);}
    void save(){
        if(!service||!snapshot||draft.empty()||locked())return;
        const auto serial=service->status().serial;
        if(service->commit(snapshot->revision,changes())){saving=true;commitSerial=serial;note.clear();}
        else{note="The settings service is busy; try again in a moment.";noteColor=Color::Muted;}
    }
    void saveAppearancePreset(){
        if(!service||!snapshot||locked())return;
        const std::string name=appearancePresetName;std::string error;
        if(!NorthlightAppearancePresets::validName(name,error)){note=error;noteColor=Color::Bad;return;}
        const auto serial=service->status().serial;
        if(service->savePreset(name,snapshot->revision,changes())){savingAppearancePreset=true;appearancePresetSerial=serial;note.clear();}
        else{note="The settings service is busy; try again in a moment.";noteColor=Color::Muted;}
    }
    void revert(){draft.clear();live.clear();wantPreview=false;wantClear=true;note.clear();}
    std::vector<const Field*> fields(unsigned file,const std::string* section=nullptr)const{
        std::vector<const Field*> out;if(!snapshot)return out;
        for(const auto& f:snapshot->fields)if(f.file==file&&(!section||f.section==*section))out.push_back(&f);
        return out;
    }

    // ---- Rows ----
    void marker(const Field& f){
        const float h=ImGui::GetFrameHeight(),r=std::max(2.5f,h*0.15f);
        const ImVec2 p=ImGui::GetCursorScreenPos(),c(p.x+r+1,p.y+h*0.5f);
        ImDrawList* dl=ImGui::GetWindowDrawList();
        if(draft.edited(f))dl->AddCircleFilled(c,r,u32(Color::Edited));
        else if(draft.overrides(f))dl->AddCircleFilled(c,r,u32(Color::Accent));
        else dl->AddCircle(c,r,u32(Color::Muted),0,std::max(1.f,scale));
        ImGui::Dummy(ImVec2(2*r+2,h));ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
    }
    void tooltip(const Field& f,D::Block block,const std::string& missing){
        if(!ImGui::BeginTooltip())return;
        ImGui::PushTextWrapPos(ImGui::GetFontSize()*30);
        ImGui::TextUnformatted(fieldLabel(f).c_str());
        ImGui::TextColored(Color::Muted,"%s  [%s]  %s",fileName(f.file),f.section.c_str(),f.key.c_str());
        ImGui::Separator();
        const auto label=[&](const std::string& value){
            if(f.kind=="check" && (value=="0" || value=="1"))return std::string(value=="1"?"On":"Off");
            return f.kind=="enum" || D::modeField(f)?optionLabel(f,value):value;
        };
        const auto shown=label(draft.effective(f));
        ImGui::Text("Value: %s%s",shown.c_str(),draft.edited(f)?"  (unsaved)":"");
        ImGui::Text("Saved: %s   Running: %s",label(draft.saved(f)).c_str(),f.available?label(f.active).c_str():"unavailable");
        ImGui::Text("Factory: %s   Inherits: %s",label(f.factory).c_str(),label(f.inherited).c_str());
        ImGui::TextColored(Color::Muted,"Source: %s%s",originText(f.origin),draft.overrides(f)?"":" - not written to the file");
        auto help=Help.find(f.key);
        if(help!=Help.end())for(const char* line:help->second){ImGui::Spacing();ImGui::TextWrapped("%s",line);}
        ImGui::Spacing();
        if(f.apply=="R")ImGui::TextColored(Color::Restart,draft.restartPending(f)?"Requires a game restart: the running renderer still uses %s.":"Requires a game restart.%s",draft.restartPending(f)?label(f.active).c_str():"");
        else if(f.apply=="S")ImGui::TextColored(Color::Good,"Takes effect when you save.");
        else ImGui::TextColored(Color::Good,"Applies live: previewed while you edit, kept when you save.");
        if(block==D::Block::Unavailable)ImGui::TextColored(Color::Bad,"Not available in this installation.");
        else if(block==D::Block::NeedsHdr)ImGui::TextColored(Color::Bad,"Enable HDR first.");
        else if(block==D::Block::NeedsKey && missing=="ReflectionScenery")ImGui::TextColored(Color::Muted,"Choose Scenery + sky to edit this setting.");
        else if(block==D::Block::NeedsKey){const char* l=find(Labels,missing);ImGui::TextColored(Color::Bad,"Requires: %s",l?l:missing.c_str());}
        if(D::canInherit(f)&&block==D::Block::None)ImGui::TextColored(Color::Muted,"Right-click: restore the inherited value.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    // After each item of a row (label, badge, control): its tooltip, and right-click restores the inherited value.
    void interact(const Field& f,D::Block block,const std::string& missing){
        if(!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))return;
        if(block==D::Block::None&&!locked()&&ImGui::IsMouseClicked(ImGuiMouseButton_Right))inherit(f);
        if(!ImGui::IsAnyItemActive()&&ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))tooltip(f,block,missing);
    }
    void settle(const Field& f){
        auto i=live.find(f.id());if(i==live.end()||ImGui::IsItemActive())return;
        std::string value;
        if(f.kind=="rgb"){for(unsigned c=0;c<3;++c)value+=(c?",":"")+text(f,i->second[c]);}
        else value=integral(f)?std::to_string(int(std::lround(quantize(f,i->second[0])))):text(f,i->second[0]);
        live.erase(i);set(f,value);
    }
    void control(const Field& f){
        const std::string id=f.id(),value=draft.effective(f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if(D::modeField(f)||f.kind=="enum"){
            const auto options=D::modeField(f)?D::modeOptions(f):D::split(f.options);
            if(ImGui::BeginCombo("##value",optionLabel(f,value).c_str())){
                for(const auto& o:options){const bool selected=o==value;if(ImGui::Selectable(optionLabel(f,o).c_str(),selected)&&!selected)set(f,o);if(selected)ImGui::SetItemDefaultFocus();}
                ImGui::EndCombo();
            }
            return;
        }
        if(f.kind=="check"){bool on=value=="1";if(ImGui::Checkbox("##value",&on))set(f,on?"1":"0");return;}
        auto held=live.find(id);
        const float lo=float(number(f.low)),hi=float(number(f.high));
        if(f.kind=="rgb"){
            float c[3]={0,0,0};
            if(held!=live.end())for(unsigned i=0;i<3;++i)c[i]=held->second[i];
            else{const auto parts=D::split(value);for(unsigned i=0;i<3&&i<parts.size();++i)c[i]=float(number(parts[i]));}
            ImGuiColorEditFlags flags=ImGuiColorEditFlags_Float|ImGuiColorEditFlags_NoOptions|ImGuiColorEditFlags_NoAlpha|ImGuiColorEditFlags_DisplayRGB|ImGuiColorEditFlags_InputRGB;
            if(hi>1)flags|=ImGuiColorEditFlags_HDR;
            if(ImGui::ColorEdit3("##value",c,flags)){for(float& x:c)x=std::clamp(x,lo,hi);live[id]={c[0],c[1],c[2]};}
            settle(f);return;
        }
        if(integral(f)){
            int v=held!=live.end()?int(std::lround(held->second[0])):int(std::lround(number(value,lo)));
            if(ImGui::SliderInt("##value",&v,int(lo),int(hi),"%d",ImGuiSliderFlags_AlwaysClamp))live[id]={float(v),0,0};
        }else{
            float v=held!=live.end()?held->second[0]:float(number(value,lo));char format[16];std::snprintf(format,sizeof format,"%%.%df",decimals(f));
            if(ImGui::SliderFloat("##value",&v,lo,hi,format,ImGuiSliderFlags_AlwaysClamp))live[id]={v,0,0};
        }
        settle(f);
    }
    // One table row: marker, label (amber while edited, "R" for restart-only keys), control.
    void row(const Field& f,const char* label=nullptr){
        std::string missing;const auto block=D::blocked(draft,f,missing);
        ImGui::PushID(f.id().c_str());
        ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
        marker(f);
        ImGui::AlignTextToFramePadding();
        const std::string name=label?label:rowLabel(f);
        if(block!=D::Block::None)ImGui::TextDisabled("%s",name.c_str());
        else if(draft.edited(f))ImGui::TextColored(Color::Edited,"%s",name.c_str());
        else ImGui::TextUnformatted(name.c_str());
        interact(f,block,missing);
        if(f.apply=="R"){ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x*2);ImGui::TextColored(draft.restartPending(f)?Color::Restart:Color::Muted,"R");interact(f,block,missing);}
        ImGui::TableSetColumnIndex(1);
        ImGui::BeginDisabled(block!=D::Block::None||locked());
        control(f);
        ImGui::EndDisabled();
        interact(f,block,missing);
        ImGui::PopID();
    }
    bool beginRows(const char* id){
        if(!ImGui::BeginTable(id,2,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_RowBg))return false;
        ImGui::TableSetupColumn("label",ImGuiTableColumnFlags_WidthStretch,1.2f);
        ImGui::TableSetupColumn("value",ImGuiTableColumnFlags_WidthStretch,1.0f);
        return true;
    }
    // A titled group: optional master checkbox in the title, a Reset button, its rows and a "Fine tuning" fold.
    void group(const Group& g,const std::map<std::string,const Field*>& byKey,std::map<std::string,bool>& used){
        auto take=[&](const char* list){std::vector<const Field*> out;for(const auto& k:words(list)){auto i=byKey.find(k);if(i!=byKey.end()&&!used[k]){out.push_back(i->second);used[k]=true;}}return out;};
        const Field* master=nullptr;
        if(g.master){auto i=byKey.find(g.master);if(i!=byKey.end()&&!used[g.master]){master=i->second;used[g.master]=true;}}
        const auto main=take(g.fields),advanced=take(g.advanced);
        if(!master&&main.empty()&&advanced.empty())return;
        ImGui::PushID(g.id);
        ImGui::Spacing();
        std::vector<const Field*> all=main;all.insert(all.end(),advanced.begin(),advanced.end());if(master)all.insert(all.begin(),master);
        if(master){
            std::string missing;const auto block=D::blocked(draft,*master,missing);
            ImGui::PushID(master->id().c_str());
            marker(*master);
            ImGui::BeginDisabled(block!=D::Block::None||locked());
            ImGui::PushStyleColor(ImGuiCol_Text,draft.edited(*master)?Color::Edited:Color::Heading);
            bool on=draft.effective(*master)=="1";
            if(ImGui::Checkbox(g.title,&on))set(*master,on?"1":"0");
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            interact(*master,block,missing);
            ImGui::PopID();
        }else{ImGui::AlignTextToFramePadding();ImGui::TextColored(Color::Heading,"%s",g.title);}
        bool resettable=false;for(const Field* f:all)if(D::canInherit(*f))resettable=true;
        alignRight(buttonWidth("Reset"));
        ImGui::BeginDisabled(!resettable||locked());
        if(ImGui::SmallButton("Reset"))for(const Field* f:all)if(D::canInherit(*f)){draft.inherit(*f);live.erase(f->id());edited(*f);}
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Restore the inherited values of this group.");
        ImGui::Separator();
        if(!main.empty()&&beginRows("rows")){for(const Field* f:main)row(*f);ImGui::EndTable();}
        if(!advanced.empty()){
            char title[64];std::snprintf(title,sizeof title,"Fine tuning (%u)",unsigned(advanced.size()));
            if(ImGui::TreeNodeEx("fine",ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_NoTreePushOnOpen,"%s",title)&&beginRows("advanced")){
                for(const Field* f:advanced)row(*f);ImGui::EndTable();
            }
        }
        ImGui::PopID();
    }
    void loose(const char* id,const std::vector<const Field*>& rest){
        if(rest.empty())return;
        ImGui::Spacing();
        if(beginRows(id)){for(const Field* f:rest)row(*f);ImGui::EndTable();}
    }

    // ---- Pages ----
    // Quality keys no section names (a newer schema): shown on the Performance page.
    std::vector<const Field*> unsortedQuality()const{
        std::vector<std::string> known{"Preset"};for(const auto& s:QualitySections)for(auto& k:sectionKeys(s))known.push_back(k);
        std::vector<const Field*> out;for(const Field* f:fields(D::Quality))if(std::find(known.begin(),known.end(),f->key)==known.end())out.push_back(f);
        return out;
    }
    // The fields a page shows (Sun & moon: every region), for its sidebar markers and Defaults.
    std::vector<const Field*> buildPageFields(Page p)const{
        std::vector<const Field*> out;
        if(p==Page::Overview){if(const Field* f=draft.quality("Preset"))out.push_back(f);for(auto& k:words(QuickKeys))if(const Field* f=draft.quality(k))out.push_back(f);return out;}
        if(p==Page::Celestial||p==Page::Atmosphere){
            if(p==Page::Atmosphere)if(const Field* f=draft.field(D::Celestial,"Appearance","Palette"))out.push_back(f);
            for(const Field* f:fields(D::Celestial)){
                const bool appearance=std::find(std::begin(NorthlightSettings::AppearanceKeys),std::end(NorthlightSettings::AppearanceKeys),f->key)!=std::end(NorthlightSettings::AppearanceKeys);
                if(appearance==(p==Page::Atmosphere))out.push_back(f);
            }
            if(p==Page::Celestial)return out;
        }
        if(p==Page::ShadowRange)return fields(D::ShadowRange);
        if(p==Page::Renderer)return fields(D::Renderer);
        if(p==Page::Water){
            auto rows=fields(NorthlightSettings::WaterHostFile);
            const auto tuning=fields(NorthlightSettings::WaterFile);
            rows.insert(rows.end(),tuning.begin(),tuning.end());return rows;
        }
        if(const Section* s=qualitySection(Pages[unsigned(p)].quality))for(auto& k:sectionKeys(*s))if(const Field* f=draft.quality(k))out.push_back(f);
        if(p==Page::Performance){auto extra=unsortedQuality();out.insert(out.end(),extra.begin(),extra.end());}
        return out;
    }
    void refreshPageFields()const{
        if(pageFieldsSnapshot==snapshot.get())return;
        for(auto& pageFields:pageFieldCache)pageFields.clear();
        pageFieldsSnapshot=nullptr;
        if(!snapshot)return;
        for(unsigned i=0;i<std::size(pageFieldCache);++i)
            pageFieldCache[i]=buildPageFields(Page(i));
        pageFieldsSnapshot=snapshot.get();
    }
    const std::vector<const Field*>& pageFields(Page p)const{
        refreshPageFields();
        return pageFieldCache[unsigned(p)];
    }
    void titled(const char* title,const std::vector<const Field*>& rows){
        if(rows.empty())return;
        ImGui::Spacing();ImGui::AlignTextToFramePadding();ImGui::TextColored(Color::Heading,"%s",title);ImGui::Separator();
        ImGui::PushID(title);if(beginRows("rows")){for(const Field* f:rows)row(*f);ImGui::EndTable();}ImGui::PopID();
    }
    void qualityPage(const Section& sec,bool withUnsorted){
        std::map<std::string,const Field*> mine;for(const auto& k:sectionKeys(sec))if(const Field* f=draft.quality(k))mine[k]=f;
        std::map<std::string,bool> used;
        for(const auto& g:sec.groups)group(g,mine,used);
        std::vector<const Field*> rest;for(const auto& k:words(sec.keys))if(mine.count(k)&&!used[k]){rest.push_back(mine[k]);used[k]=true;}
        titled(sec.title,rest);
        if(withUnsorted)titled("Other",unsortedQuality());
    }
    void overviewPage(){
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(Color::Muted,"Choose starting settings, then adjust effects in the categories on the left.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();ImGui::TextColored(Color::Heading,"Quality preset");ImGui::Separator();
        const Field* preset=draft.quality("Preset");
        const std::string current=preset?draft.effective(*preset):"";
        for(const auto& p:Presets){
            ImGui::PushID(p.key);
            const bool here=current==p.key;
            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();
            if(here)ImGui::TextColored(Color::Accent,"%s  (current)",p.key);else ImGui::TextUnformatted(p.key);
            ImGui::TextColored(Color::Muted,"%s",p.text);
            ImGui::EndGroup();
            ImGui::SameLine();alignRight(buttonWidth("Choose..."));
            ImGui::BeginDisabled(here||!preset||locked());
            if(ImGui::Button("Choose...")){presetChoice=p.key;confirm=Confirm::Preset;}
            ImGui::EndDisabled();
            ImGui::PopID();
            ImGui::Separator();
        }
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(Color::Muted,"Choosing a preset resets your quality changes to the preset's values. You will be asked to confirm.");
        ImGui::PopTextWrapPos();
        std::vector<const Field*> quick;for(auto& k:words(QuickKeys))if(const Field* f=draft.quality(k))quick.push_back(f);
        titled("Quick toggles",quick);
        ImGui::Spacing();ImGui::TextColored(Color::Heading,"Summary");ImGui::Separator();
        unsigned set=0;if(snapshot)for(const auto& f:snapshot->fields)if(f.overrideValue!="~")++set;
        ImGui::Text("%u setting%s set in the INI files, %u unsaved change%s.",set,set==1?"":"s",unsigned(draft.size()),draft.size()==1?"":"s");
        if(const auto n=restartPending())ImGui::TextColored(Color::Restart,"%u change%s apply after a game restart.",unsigned(n),n==1?"":"s");
    }
    void rendererPage(){
        std::map<std::string,const Field*> byKey;for(const Field* f:fields(D::Renderer))byKey[f->key]=f;
        std::map<std::string,bool> used;
        if(auto b=byKey.find("Backend");b!=byKey.end()){used["Backend"]=true;titled("Renderer",{b->second});}
        for(const auto& g:HdrGroups)group(g,byKey,used);
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(Color::Muted,"HDR needs DXVK 2 or 3, the separate render thread, windowed mode and HDR output from the system or Wine. Restart after switching HDR on or off; its sliders apply live.");
        ImGui::PopTextWrapPos();
        std::vector<const Field*> rest;for(auto& k:byKey)if(!used[k.first])rest.push_back(k.second);
        titled("Other",rest);
    }
    void waterPage(){
        ImGui::TextWrapped("The Water preset controls Tideglass independently of the Northlight quality preset. Save to apply water settings. Restart after switching Tideglass on or off.");
        titled("Renderer",fields(NorthlightSettings::WaterHostFile));
        auto waterRows=[&](const char* title,const char* keys){
            std::vector<const Field*> rows;
            for(const auto& key:words(keys)){
                if(const Field* f=draft.field(NorthlightSettings::WaterFile,"Water",key))rows.push_back(f);
            }
            titled(title,rows);
        };
        const auto water=[&](const char* key){return draft.field(NorthlightSettings::WaterFile,"Water",key);};
        const Field* scenery=water("ReflectionScenery");
        const Field* resolution=water("ReflectionResolution");
        const Field* distance=water("ReflectionDistance");
        const Field* fine=water("FineHighlights");
        const Field* details=water("ReflectionSceneryDetails");
        const Field* blur=water("SurfaceBlur");
        if(scenery && resolution && distance && fine && details && blur){
            const char* current=D::waterPreset(draft.effective(*scenery),draft.effective(*resolution),draft.effective(*distance),draft.effective(*fine),draft.effective(*details),draft.effective(*blur));
            ImGui::TextColored(Color::Heading,"Performance");ImGui::Separator();
            ImGui::BeginDisabled(locked());
            if(ImGui::BeginCombo("Water preset",current)){
                for(const auto& tier:D::WaterTiers){
                    const bool selected=std::string(current)==tier.name;
                    if(ImGui::Selectable(tier.name,selected)){
                        set(*scenery,tier.scenery);set(*resolution,tier.resolution);set(*distance,tier.distance);set(*fine,tier.fineHighlights);set(*details,tier.sceneryDetails);set(*blur,tier.depthBlur);
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            loose("water-performance",{scenery,resolution,distance,details,fine,blur});
        }
        waterRows("Waves and foam","WaveStrength WaveSpeed WaveCrestShape WaveCrestSharpness RippleStrength FoamStrength OpenWaterCrestFoam");
        waterRows("Reflections","Reflection NearReflection");
        waterRows("Clarity and underwater","Clarity Underwater UnderwaterVisibility UnderwaterScenery");
        waterRows("Other liquids","Slime Lava");
        if(water("Slime")&&pickRegion(waterRegion,true)){
            const auto id=NorthlightSettings::liquidAreaKey(waterRegion);
            if(const Field* f=draft.field(NorthlightSettings::WaterFile,"RegionalSlime",id))regionalControl(*f,"Slime pigment and movement");
        }
    }
    void drawPage(){
        switch(page){
        case Page::Overview:overviewPage();break;
        case Page::Celestial:celestialTab();break;
        case Page::ShadowRange:shadowRangeTab();break;
        case Page::Renderer:rendererPage();break;
        case Page::Water:waterPage();break;
        case Page::Atmosphere:
            ImGui::Spacing();ImGui::TextColored(Color::Heading,"World appearance");ImGui::Separator();
            if(const Field* palette=draft.field(D::Celestial,"Appearance","Palette"))loose("world-palette",{palette});
            ImGui::TextWrapped("Default keeps Northlight's current regional look and your personal region edits. Other looks supply a whole-world base; personal regional edits remain layered above it.");
            if(ImGui::Button("Save current look as a named copy...")){appearancePresetName[0]='\0';appearancePresetModal=true;}
            ImGui::SameLine();
            ImGui::TextColored(Color::Muted,"Portable INI files: appearance-presets");
            if(pickRegion()){
                for(const char* key:{"fog_tint_strength","shadow_contrast"})
                    if(const Field* f=draft.field(D::Celestial,region,key))regionalControl(*f,rowLabel(*f).c_str());
                if(const Field* f=draft.field(D::Celestial,region,"fog_tint"))loose("fog-colour",{f});
                if(const Field* strength=draft.field(D::Celestial,region,"tone_curve_strength")){
                    regionalControl(*strength,"Tone curve strength");
                    for(unsigned time=0;time<2;++time)if(ImGui::TreeNode(time?"Night tone curve":"Daylight tone curve")){
                        ImGui::BeginDisabled(locked());
                        curveEditor(time);
                        ImGui::EndDisabled();
                        ImGui::TreePop();
                    }
                }
            }
            if(const Section* s=qualitySection("atmosphere")){
                ImGui::Spacing();ImGui::TextColored(Color::Heading,"Atmosphere and weather");ImGui::Separator();
                qualityPage(*s,false);
            }
            break;
        default:if(const Section* s=qualitySection(Pages[unsigned(page)].quality))qualityPage(*s,page==Page::Performance);break;
        }
    }
    // Left column: the pages; amber dot = unsaved edits there, R = a restart-only change pending there.
    void sidebar(){
        const float h=ImGui::GetFrameHeight(),r=std::max(2.5f,h*0.14f);
        for(const auto& info:Pages){
            if(info.page==Page::Water&&!draft.field(NorthlightSettings::WaterHostFile,"WaterIntegration","Enable"))continue;
            bool edited=false,restart=false;
            for(const Field* f:pageFields(info.page)){edited|=draft.edited(*f);restart|=draft.restartPending(*f);}
            if(ImGui::Selectable(info.title,page==info.page,0,ImVec2(0,h)))page=info.page;
            const ImVec2 a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
            ImDrawList* dl=ImGui::GetWindowDrawList();float x=b.x-ImGui::GetStyle().ItemInnerSpacing.x-r;
            if(edited){dl->AddCircleFilled(ImVec2(x,(a.y+b.y)*0.5f),r,u32(Color::Edited));x-=2*r+ImGui::GetStyle().ItemInnerSpacing.x;}
            if(restart){const ImVec2 t=ImGui::CalcTextSize("R");dl->AddText(ImVec2(x-t.x*0.5f-r,(a.y+b.y-t.y)*0.5f),u32(Color::Restart),"R");}
        }
    }
    std::vector<std::string> sections(unsigned file)const{
        std::vector<std::string> out;if(!snapshot)return out;std::set<std::string> unique;
        for(const auto& f:snapshot->fields)if(f.file==file)unique.insert(f.section);
        out.assign(unique.begin(),unique.end());
        std::sort(out.begin(),out.end(),[](const std::string& a,const std::string& b){
            const auto ag=regionGroup(a),bg=regionGroup(b);const int ao=regionGroupOrder(ag),bo=regionGroupOrder(bg);
            if(ao!=bo)return ao<bo;
            if(ag!=bg)return ag<bg;
            const auto al=regionLabel(a),bl=regionLabel(b);return al==bl?a<b:al<bl;});
        return out;
    }
    void refreshRegionChoices(){
        if(!snapshot||regionChoicesSnapshot==snapshot.get())return;
        regionChoices.clear();waterRegionChoices.clear();
        std::set<std::string> waterAreaIds;
        for(const auto& section:sections(D::Celestial)){
            if(section=="Appearance")continue;
            RegionChoice choice{section,regionLabel(section),regionGroup(section),{}};
            choice.search=folded(choice.label+" "+section);
            regionChoices.push_back(choice);
            if(mapDefaultSection(section))continue;
            const auto id=section=="default"?section:NorthlightSettings::liquidAreaKey(section);
            if(!draft.field(NorthlightSettings::WaterFile,"RegionalSlime",id)||!waterAreaIds.insert(id).second)continue;
            if(section.find("@inside")!=section.npos){
                const auto root=section.substr(0,section.size()-7);
                if(const char* name=find(Zones,root))choice.label=std::string(name)+"  ("+section+")";
                choice.search=folded(choice.label+" "+section);
            }
            waterRegionChoices.push_back(std::move(choice));
        }
        regionChoicesSnapshot=snapshot.get();
    }
    bool pickRegion(std::string& selected,bool waterOnly=false){
        refreshRegionChoices();
        auto& regions=waterOnly?waterRegionChoices:regionChoices;
        if(regions.empty())return false;
        const bool hideInterior=page==Page::Celestial;
        const auto visible=[&](const RegionChoice& choice){
            return !hideInterior||choice.section.find("@inside")==choice.section.npos;
        };
        auto current=std::find_if(regions.begin(),regions.end(),
            [&](const RegionChoice& choice){return choice.section==selected&&visible(choice);});
        if(current==regions.end()){
            current=std::find_if(regions.begin(),regions.end(),visible);
            if(current==regions.end())return false;
            selected=current->section;
        }
        auto& filter=waterOnly?waterRegionFilter:regionFilter;
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Region");ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if(ImGui::BeginCombo(waterOnly?"##water-region":"##region",current->label.c_str(),
                             ImGuiComboFlags_HeightLarge)){
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint(waterOnly?"##water-region-search":"##region-search",
                                     "Search by region name or INI key",filter.data(),filter.size());
            ImGui::Separator();
            const std::string query=folded(filter.data());std::string visibleGroup;bool found=false;
            for(const auto& choice:regions){
                if(!visible(choice))continue;
                if(!query.empty()&&choice.search.find(query)==choice.search.npos)continue;
                if(choice.group!=visibleGroup){
                    if(found)ImGui::Spacing();
                    ImGui::TextDisabled("%s",choice.group.c_str());visibleGroup=choice.group;
                }
                const bool active=choice.section==selected;
                if(ImGui::Selectable(choice.label.c_str(),active)){selected=choice.section;filter[0]='\0';}
                if(active)ImGui::SetItemDefaultFocus();found=true;
            }
            if(!found)ImGui::TextDisabled("No matching regions.");
            ImGui::EndCombo();
        }
        return true;
    }
    bool pickRegion(){return pickRegion(region);}
    void curveEditor(unsigned time){
        ImGui::PushID(int(time));
        const char* channels[]={"Brightness","Red","Green","Blue"};
        ImGui::SetNextItemWidth(170*scale);
        ImGui::Combo("Channel",&curveChannel,channels,4);
        NorthlightTone::Curve curve;
        const Field* fields[5]{};
        for(unsigned i=0;i<5;++i){
            const auto key=NorthlightTone::key(time,curveChannel,i);
            fields[i]=draft.field(D::Celestial,region,key);
            if(!fields[i]){ImGui::PopID();return;}
            float lo,hi;
            *NorthlightTone::parameter(curve,i,lo,hi)=float(number(draft.effective(*fields[i])));
        }
        const ImVec2 origin=ImGui::GetCursorScreenPos();
        const ImVec2 size(ImGui::GetContentRegionAvail().x,135*scale);
        const float pad=6*scale,w=std::max(size.x-2*pad,1.f),h=size.y-2*pad;
        auto pos=[&](float x,float y){return ImVec2(origin.x+pad+x*w,origin.y+pad+(1-y)*h);};
        auto nodeX=[&](int i){return i==0?curve.lowInput:i*.25f;};
        auto nodeY=[&](int i){return i==0?NorthlightTone::lowOutput(curve):curve.levels[i-1];};
        ImGui::InvisibleButton("Curve graph",size);
        const auto mouse=ImGui::GetIO().MousePos;
        if(ImGui::IsItemActivated()){
            dragPoint=-1;
            curveDragged=false;
            float best=16*scale;
            for(int i=0;i<4;++i){
                const auto pt=pos(nodeX(i),nodeY(i));
                const float dist=std::hypot(mouse.x-pt.x,mouse.y-pt.y);
                if(dist<best){best=dist;dragPoint=i;selectedPoint=i;curveDragOffset={pt.x-mouse.x,pt.y-mouse.y};}
            }
        }
        if(dragPoint>=0&&ImGui::IsItemActive()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left,1.f))curveDragged=true;
        if(dragPoint>=0&&curveDragged&&(ImGui::IsItemActive()||ImGui::IsItemDeactivated())){
            const bool released=ImGui::IsItemDeactivated();
            const bool update=released||ImGui::GetTime()>=nextCurvePreview;
            const float y=std::clamp(1-(mouse.y+curveDragOffset.y-origin.y-pad)/h,0.f,1.f);
            auto change=[&](unsigned index,float value){
                float lo,hi;float* target=NorthlightTone::parameter(curve,index,lo,hi);
                *target=std::clamp(value,lo,hi);
                if(update){
                    const auto& field=*fields[index];const auto rounded=text(field,*target);
                    if(std::abs(number(rounded)-number(draft.effective(field)))>.00001)set(field,rounded);
                }
            };
            if(dragPoint==0){
                change(NorthlightTone::LowInput,(mouse.x+curveDragOffset.x-origin.x-pad)/w);
                change(NorthlightTone::LowBend,NorthlightTone::bendForOutput(curve,y));
            }else change(NorthlightTone::Shadow+dragPoint-1,y);
            if(update)nextCurvePreview=ImGui::GetTime()+.12;
        }
        if(ImGui::IsItemDeactivated())dragPoint=-1;
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(27,33,39,255),3);
        for(int i=1;i<4;++i){
            draw->AddLine(pos(i*.25f,0),pos(i*.25f,1),IM_COL32(57,67,75,255));
            draw->AddLine(pos(0,i*.25f),pos(1,i*.25f),IM_COL32(57,67,75,255));
        }
        draw->AddLine(pos(0,0),pos(1,1),IM_COL32(100,109,115,255));
        const ImU32 inks[]={IM_COL32(226,223,209,255),IM_COL32(255,139,128,255),IM_COL32(130,220,165,255),IM_COL32(133,186,255,255)};
        ImVec2 last=pos(0,0);
        for(int i=1;i<=80;++i){
            const float x=i/80.f;
            const auto pt=pos(x,NorthlightTone::sample(curve,x));
            draw->AddLine(last,pt,inks[curveChannel],2*scale);last=pt;
        }
        for(int i=0;i<4;++i)
            draw->AddCircleFilled(pos(nodeX(i),nodeY(i)),(selectedPoint==i?5:3.5f)*scale,inks[curveChannel]);
        ImGui::TextDisabled("Drag a point. The first point also moves horizontally.");
        ImGui::Text("Input %.0f%%    Output %.1f%%",100*nodeX(selectedPoint),100*nodeY(selectedPoint));
        if(ImGui::SmallButton("Inherit selected point")){
            if(selectedPoint==0){inherit(*fields[0]);inherit(*fields[1]);}
            else inherit(*fields[selectedPoint+1]);
        }
        ImGui::SameLine();ImGui::Button("Hold to compare");
        if(ImGui::IsItemActive())compareRequested=true;
        if(ImGui::TreeNode("Point values")){
            if(beginRows("points")){
                row(*fields[0],"Low input");row(*fields[1],"Low bend (0.50 follows shadows)");
                row(*fields[2],"Shadows (25%)");row(*fields[3],"Midtones (50%)");row(*fields[4],"Highlights (75%)");
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }
        if(const Field* f=draft.field(D::Celestial,region,time?"night_vibrance":"day_vibrance"))
            loose("colour-grade",{f});
        ImGui::PopID();
    }
    void regionalControl(const Field& f,const char* title){
        ImGui::PushID(f.id().c_str());
        ImGui::TextUnformatted(title);
        auto value=draft.effective(f);
        const char* mode=!draft.overrides(f)?"Inherit":number(value)==0?"Off":"Tuned";
        if(ImGui::BeginCombo("Mode",mode)){
            for(const char* choice:{"Inherit","Off","Tuned"})if(ImGui::Selectable(choice,std::string(choice)==mode)){
                if(std::string(choice)=="Inherit")inherit(f);
                else{draft.pin(f,std::string(choice)=="Off"?"0":number(value)>0?value:"0.5");edited(f);}
            }
            ImGui::EndCombo();
        }
        if(std::string(mode)=="Tuned")control(f);
        else if(std::string(mode)=="Inherit")ImGui::TextDisabled("Inherited strength: %s",value.c_str());
        ImGui::PopID();
    }
    void celestialTab(){
        if(!pickRegion())return;
        ImGui::TextColored(Color::Muted,"Unset values follow the parent region.");
        std::map<std::string,const Field*> byKey;for(const Field* f:fields(D::Celestial,&region))byKey[f->key]=f;
        std::map<std::string,bool> used;
        ImGui::PushID(region.c_str());
        for(const auto& key:NorthlightSettings::AppearanceKeys)used[key]=true;
        for(const auto& g:CelestialGroups)group(g,byKey,used);
        std::vector<const Field*> rest;for(auto& k:byKey)if(!used[k.first])rest.push_back(k.second);
        loose("rest",rest);
        ImGui::PopID();
    }
    void shadowRangeTab(){
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(Color::Muted,"Terrain shadow reach per region, in yards. Regions not listed use the default reach.");
        ImGui::PopTextWrapPos();
        const auto regions=sections(D::ShadowRange);
        if(regions.empty()){ImGui::TextDisabled("No shadow range profiles.");return;}
        if(beginRows("ranges")){
            for(const auto& r:regions)if(const Field* f=draft.field(D::ShadowRange,r,"terrain_radius"))row(*f,regionLabel(r).c_str());
            ImGui::EndTable();
        }
    }

    // ---- Footer ----
    std::size_t restartPending()const{
        std::size_t n=0;if(!snapshot)return n;
        for(const auto& f:snapshot->fields)if(draft.restartPending(f))++n;
        return n;
    }
    void statusLine(){
        using S=NorthlightSettings::Status::State;
        if(!service){ImGui::TextColored(Color::Bad,"Settings are unavailable (see northlight-renderer.log).");return;}
        if(!note.empty()){ImGui::TextColored(noteColor,"%s",note.c_str());return;}
        if(!snapshot){ImGui::TextColored(Color::Muted,status.state==S::Error?"%s":"Loading settings...",status.message.c_str());return;}
        if(saving||status.state==S::Busy)ImGui::TextColored(Color::Edited,"%s",saving?"Saving...":"Working...");
        else if(status.state==S::Error)ImGui::TextColored(Color::Bad,"%s",status.message.empty()?"The last request failed.":status.message.c_str());
        else if(!draft.empty())ImGui::TextColored(Color::Edited,"%u unsaved change%s%s",unsigned(draft.size()),draft.size()==1?"":"s",previewing?" (previewing)":"");
        else if(status.state==S::Saved)ImGui::TextColored(Color::Good,"%s",status.message.empty()?"Saved.":status.message.c_str());
        else ImGui::TextColored(Color::Muted,"No unsaved changes.");
        if(status.restartRequired||restartPending()){ImGui::SameLine();ImGui::TextColored(Color::Restart,"  R: restart required");}
    }
    void legend(){
        const float h=ImGui::GetTextLineHeight(),r=std::max(2.5f,h*0.2f);
        auto dot=[&](const ImVec4& c,bool filled,const char* label){
            const ImVec2 p=ImGui::GetCursorScreenPos();const ImVec2 at(p.x+r+1,p.y+ImGui::GetFrameHeight()*0.5f);
            if(filled)ImGui::GetWindowDrawList()->AddCircleFilled(at,r,u32(c));else ImGui::GetWindowDrawList()->AddCircle(at,r,u32(c),0,std::max(1.f,scale));
            ImGui::Dummy(ImVec2(2*r+2,ImGui::GetFrameHeight()));ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::AlignTextToFramePadding();ImGui::TextColored(Color::Muted,"%s",label);ImGui::SameLine();
        };
        const auto& st=ImGui::GetStyle();
        const float width=4*(2*r+2+st.ItemInnerSpacing.x+st.ItemSpacing.x)+ImGui::CalcTextSize("set").x+ImGui::CalcTextSize("inherited").x+ImGui::CalcTextSize("unsaved").x+ImGui::CalcTextSize("R restart").x;
        alignRight(width);
        dot(Color::Accent,true,"set");dot(Color::Muted,false,"inherited");dot(Color::Edited,true,"unsaved");
        ImGui::TextColored(Color::Restart,"R");ImGui::SameLine(0,st.ItemInnerSpacing.x);ImGui::TextColored(Color::Muted,"restart");
    }
    void footer(){
        ImGui::Separator();
        statusLine();
        // Only while the service is idle: a queued preview would otherwise finish after the commit and hide its result.
        const bool idle=status.state!=NorthlightSettings::Status::State::Busy;
        ImGui::BeginDisabled(draft.empty()||locked()||!service||!idle);
        const bool dirty=!draft.empty()&&!locked()&&idle;
        if(dirty){ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.20f,0.42f,0.70f,1));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(0.26f,0.50f,0.80f,1));}
        if(ImGui::Button("Save"))save();
        if(dirty)ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Write the changes to the INI files. Live settings stay applied; R settings apply after a restart.");
        ImGui::SameLine();
        ImGui::BeginDisabled((draft.empty()&&!previewing)||locked());
        if(ImGui::Button("Revert"))revert();
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Discard the unsaved changes and return to the saved settings.");
        ImGui::SameLine();
        ImGui::BeginDisabled(locked());
        if(ImGui::Button("Defaults..."))confirm=Confirm::Factory;
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",factoryScope().c_str());
        ImGui::SameLine();legend();
    }
    std::string factoryScope()const{
        if(page==Page::Celestial)return "Restore the factory sun and moon values of the selected region ("+regionLabel(region)+").";
        if(page==Page::ShadowRange)return "Restore the factory shadow reach of every region.";
        return std::string("Restore the factory values of every setting on the ")+Pages[unsigned(page)].title+" page.";
    }
    void factoryDefaults(){
        const auto& targets=pageFields(page);
        for(const Field* f:targets)if(f->available&&(f->file!=D::Celestial||f->section==region)){draft.factory(*f);live.erase(f->id());edited(*f);}
    }
    // The old menu's preset choice: quality overrides back to inherit, optionally Sun & moon and Shadow range to factory.
    void choosePreset(){
        const Field* preset=draft.quality("Preset");if(!preset)return;
        for(const Field* f:fields(D::Quality))if(f->key!="Preset"&&D::canInherit(*f)){draft.inherit(*f);live.erase(f->id());edited(*f);}
        if(presetCelestial)for(const Field* f:pageFields(Page::Celestial))if(f->available){draft.factory(*f);live.erase(f->id());edited(*f);}
        if(presetRange)for(const Field* f:fields(D::ShadowRange))if(f->available){draft.factory(*f);live.erase(f->id());edited(*f);}
        set(*preset,presetChoice);
    }
    void modals(){
        if(confirm==Confirm::Preset)ImGui::OpenPopup("Choose preset?");
        if(confirm==Confirm::Factory)ImGui::OpenPopup("Restore defaults?");
        if(appearancePresetModal){ImGui::OpenPopup("Save appearance copy");appearancePresetModal=false;}
        confirm=Confirm::None;
        const ImVec2 centre=ImGui::GetMainViewport()->GetCenter();
        auto ask=[&](const char* title,const char* body,const char* yes,auto&& accept){
            ImGui::SetNextWindowPos(centre,ImGuiCond_Appearing,ImVec2(0.5f,0.5f));
            if(!ImGui::BeginPopupModal(title,nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings))return;
            ImGui::PushTextWrapPos(ImGui::GetFontSize()*26);ImGui::TextWrapped("%s",body);ImGui::PopTextWrapPos();
            ImGui::TextColored(Color::Muted,"Nothing is written until you press Save.");
            ImGui::Spacing();
            if(ImGui::Button(yes)){accept();ImGui::CloseCurrentPopup();}
            ImGui::SameLine();
            if(ImGui::Button("Cancel")||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        };
        ImGui::SetNextWindowPos(centre,ImGuiCond_Appearing,ImVec2(0.5f,0.5f));
        if(ImGui::BeginPopupModal("Choose preset?",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings)){
            ImGui::PushTextWrapPos(ImGui::GetFontSize()*26);
            ImGui::TextWrapped("Preset: %s. Every quality setting you have changed returns to the preset's value.",presetChoice.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Checkbox("Also reset Sun & moon to factory values",&presetCelestial);
            ImGui::Checkbox("Also reset Shadow range to factory values",&presetRange);
            ImGui::TextColored(Color::Muted,"Renderer & HDR settings are kept. Nothing is written until you press Save.");
            ImGui::Spacing();
            if(ImGui::Button("Choose preset")){choosePreset();ImGui::CloseCurrentPopup();}
            ImGui::SameLine();
            if(ImGui::Button("Cancel")||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        const std::string scope=factoryScope();
        ask("Restore defaults?",scope.c_str(),"Restore",[&]{factoryDefaults();});
        ImGui::SetNextWindowPos(centre,ImGuiCond_Appearing,ImVec2(0.5f,0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(440*scale,0),ImVec2(440*scale,FLT_MAX));
        if(ImGui::BeginPopupModal("Save appearance copy",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings)){
            ImGui::TextWrapped("Copy the current whole-world appearance, including regional differences, to a portable INI file. Existing files are never overwritten.");
            ImGui::SetNextItemWidth(280*scale);ImGui::InputText("Name",appearancePresetName,sizeof appearancePresetName);
            std::string error;const bool valid=NorthlightAppearancePresets::validName(appearancePresetName,error);
            ImGui::TextWrapped("%s",NorthlightAppearancePresets::NameRuleText);
            ImGui::TextColored(Color::Muted,"The copy is saved beside the renderer; Revert does not remove it.");
            ImGui::Spacing();
            ImGui::BeginDisabled(!valid||locked()||status.state==NorthlightSettings::Status::State::Busy);
            if(ImGui::Button("Save copy")){saveAppearancePreset();if(savingAppearancePreset)ImGui::CloseCurrentPopup();}
            ImGui::EndDisabled();ImGui::SameLine();
            if(ImGui::Button("Cancel")||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
};

SettingsPanel::SettingsPanel(std::shared_ptr<NorthlightSettings::Service> service,std::string hotkey):s(std::make_unique<State>()){s->service=std::move(service);s->hotkey=std::move(hotkey);}
SettingsPanel::~SettingsPanel()=default;
void SettingsPanel::opened(){s->wantSnapshot=true;}
void SettingsPanel::poll(){if(s->comparing){s->comparing=false;s->wantPreview=true;}s->poll();}
bool SettingsPanel::pending()const{return s->service&&(s->comparing||s->wantSnapshot||s->wantPreview||s->wantClear||s->saving||s->awaitingSnapshot||s->savingAppearancePreset);}
bool SettingsPanel::unsaved()const{return !s->draft.empty();}
void SettingsPanel::notice(float scale){
    const ImGuiViewport* view=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(view->WorkPos.x+12*scale,view->WorkPos.y+view->WorkSize.y-12*scale),ImGuiCond_Always,ImVec2(0,1));
    ImGui::SetNextWindowBgAlpha(0.55f);
    const ImGuiWindowFlags flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|
        ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav;
    if(ImGui::Begin("##NorthlightUnsaved",nullptr,flags))
        ImGui::TextColored(Color::Edited,"Northlight: unsaved settings%s (%s)",s->previewing?" preview":"",s->hotkey.c_str());
    ImGui::End();
}

bool SettingsPanel::draw(float scale){
    State& st=*s;st.scale=scale;
    st.poll();st.compareRequested=false;
    bool open=true;
    ImGui::SetNextWindowPos(ImVec2(48*scale,96*scale),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760*scale,680*scale),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(560*scale,360*scale),ImVec2(FLT_MAX,FLT_MAX));
    if(!ImGui::Begin("Northlight settings###NorthlightSettings",&open,ImGuiWindowFlags_NoCollapse)){ImGui::End();return open;}
    const auto& style=ImGui::GetStyle();
    const float footer=ImGui::GetTextLineHeightWithSpacing()+ImGui::GetFrameHeightWithSpacing()+style.ItemSpacing.y*2+style.SeparatorTextBorderSize+2;
    // Sidebar (fixed width, scaled) and the selected page; the footer below stays put on every page.
    if(ImGui::BeginChild("nav",ImVec2(180*scale,-footer),ImGuiChildFlags_Borders))st.sidebar();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::PushID(int(st.page));
    if(ImGui::BeginChild("page",ImVec2(0,-footer))){
        if(!st.snapshot)ImGui::TextDisabled(st.service?"Loading settings...":"The settings service is not running.");
        else st.drawPage();
    }
    ImGui::EndChild();
    ImGui::PopID();
    st.footer();
    st.modals();
    ImGui::End();
    if(st.comparing!=st.compareRequested){st.comparing=st.compareRequested;st.wantPreview=true;}
    return open;
}
} // namespace NorthlightOverlay
