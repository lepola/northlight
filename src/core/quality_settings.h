#pragma once
// User quality settings (northlight-quality.ini), read once at startup. Portable:
// no device or Win32 calls. Quality (and a missing/empty file) is exactly the
// 0.3.136 behaviour; every other value only removes optional CPU/GPU work,
// except the picture-only keys (GIStrength, HorizonHaze*).
#include <cctype>
#include <cstdlib>
#include <istream>
#include <sstream>
#include <string>
#include <vector>

namespace NorthlightQuality {
enum class Preset { Quality, Balanced, Performance };
struct Settings {
    Preset preset=Preset::Quality;
    unsigned minSkinnedTriangles=0,captureBudgetMiB=32,actorShadowBudgetMiB=0;
    unsigned farShadowInterval=4,nearShadowInterval=1,localLightLimit=32,pointShadows=0,pointShadowRefreshMs=0,shadowDirectionSteps=2048;
    // Where each value in Keys order came from: 'd' code default (Quality), 'p' Balanced/Performance
    // preset, 'l' legacy shadow-experiment.ini, 'f' northlight-quality.ini key. Not part of ==.
    // 0.3.138 GI: worker probe solve (rays, bounces, request step), actor/dynamic probes, solver threads.
    unsigned gi=1,giRays=64,giBounces=3,giProbeMoveStep=8,giDynamicProbes=1,giThreads=1,giFastBVH=1;
    // 0.3.142: bounce-light strength in percent; 60 = the 0.3.141 constant 0.5, 100 = maximum (0.5*100/60).
    unsigned giStrength=60;
    // 0.3.141: shadow fate tracker (diagnostic log only; never changes the image).
    // Diagnostics=0: periodic logs, diagnostic counters and pure measurement off
    // (diagnostics_switch.h); it also forces the fate tracker off.
    unsigned shadowFateDiagnostics=0,diagnostics=0;
    // 0.3.149 render-thread instrumentation (render_thread_probe.h), never a rendering decision.
    // RenderProfile=1 (needs Diagnostics=1): profiler sampler, per-frame times, replay loop split,
    // call counts. DiagReplayProbe=1 (needs RenderProfile): the sun near replays issued again into
    // a private target in alternating windows (world_replay_probe.inl). Both 0: the 0.3.148 paths.
    unsigned renderProfile=0,diagReplayProbe=0;
    // ActorShadowRadius: characters/creatures farther than this many yards from the
    // player cast no shadow (actor_shadow_selection.h Radius). 0 = no limit (0.3.144).
    unsigned actorShadowRadius=40;
    // 0.3.151 spike spreading. PointShadowFacesPerFrame: lamp cube faces refreshed per frame
    // (6 = all six in one frame, 0.3.150). StaticCacheSlices: a sun/moon static cache rect
    // redraw is spread over this many frames (1 = one frame, 0.3.150).
    unsigned pointShadowFacesPerFrame=6,staticCacheSlices=1;
    // 0.3.153 GIDistance: yards of solved GI around the probe window centre; snaps down
    // to 36,44,..,84 (giProbeGrid). 52 = the 0.3.151 14-cell window and 16^3 atlas.
    unsigned giDistance=76;
    // 0.3.153 GIProbeAhead: yards the probe window centre moves ahead of the eye along the
    // horizontal view direction; geometry, fog and everything else stay on the eye. 0 = eye.
    unsigned giProbeAhead=0;
    // Horizon haze (picture only, horizon_haze.h). HorizonHaze: strength in percent,
    // 50 = optical depth .7, 0 = the 0.3.153 image. HorizonHazeStart: % of the game's fog end
    // where terrain haze begins. HorizonHazeBand: degrees the sky band climbs. HorizonHazeTerrain
    // 0 = sky band only.
    unsigned horizonHaze=50,horizonHazeStart=75,horizonHazeBand=6,horizonHazeTerrain=1;
    // 0.3.158 ActorShadows: 0 = static mod shadows only (terrain, world-cache models): no replay
    // (character, creature, server object) shadows, the game's blob shadows return, and model
    // capture runs only for GI actor packets. Forces the replay-derived keys off (effective()).
    unsigned actorShadows=1;
    // 0.3.187 FrameDrawGates: 1 = the draw hooks skip the sky, blob and terrain shadow work in frames
    // where its per-frame preconditions are off (draw_gates.h); 0 = the 0.3.184 per-draw work. Same image.
    unsigned frameDrawGates=1;
    // 0.3.188 TranslucentActorDepth: the effects read the depth from before the first translucent Z-writing
    // actor draw (stealth, ghost pets), so the ground seen through them is lit like its surroundings.
    // 0 = depth at the first UI draw as before; 1 = before the first skinned translucent/depth-only draw; 2 = any world draw.
    unsigned translucentActorDepth=1;
    char origin[33]={'d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d','d'};
};
struct Key { const char* name; unsigned Settings::*field; unsigned low,high; unsigned preset[3]; };
// preset[] = Quality, Balanced, Performance. Quality must equal Settings{}.
inline const Key Keys[]={
    {"MinSkinnedTriangles",&Settings::minSkinnedTriangles,0,500,{0,50,100}},
    {"CaptureBudgetMiB",&Settings::captureBudgetMiB,1,32,{32,32,32}},
    {"ActorShadowBudgetMiB",&Settings::actorShadowBudgetMiB,0,32,{0,16,8}},
    {"FarShadowInterval",&Settings::farShadowInterval,1,16,{4,5,6}},
    {"NearShadowInterval",&Settings::nearShadowInterval,1,16,{1,2,2}},
    {"LocalLightLimit",&Settings::localLightLimit,8,64,{32,24,16}},
    {"PointShadows",&Settings::pointShadows,0,1,{0,0,0}}, /* 0.3.176: off in every preset (only building lights cast) */
    {"PointShadowRefreshMs",&Settings::pointShadowRefreshMs,0,100,{0,33,33}},
    {"ShadowDirectionSteps",&Settings::shadowDirectionSteps,256,2048,{2048,1024,512}},
    {"GI",&Settings::gi,0,1,{1,1,1}},
    {"GIRays",&Settings::giRays,16,64,{64,48,32}},
    {"GIBounces",&Settings::giBounces,1,3,{3,3,2}},
    {"GIProbeMoveStep",&Settings::giProbeMoveStep,8,16,{8,8,16}},
    {"GIDynamicProbes",&Settings::giDynamicProbes,0,1,{1,0,0}},
    {"GIThreads",&Settings::giThreads,1,4,{1,1,1}},
    {"GIFastBVH",&Settings::giFastBVH,0,1,{1,1,1}},
    {"GIStrength",&Settings::giStrength,0,100,{60,60,60}},
    {"ShadowFateDiagnostics",&Settings::shadowFateDiagnostics,0,1,{0,0,0}},
    {"Diagnostics",&Settings::diagnostics,0,1,{0,0,0}},
    {"RenderProfile",&Settings::renderProfile,0,1,{0,0,0}},
    {"DiagReplayProbe",&Settings::diagReplayProbe,0,1,{0,0,0}},
    {"ActorShadowRadius",&Settings::actorShadowRadius,0,200,{40,35,20}},
    {"PointShadowFacesPerFrame",&Settings::pointShadowFacesPerFrame,1,6,{6,6,6}},
    {"StaticCacheSlices",&Settings::staticCacheSlices,1,4,{1,1,1}},
    {"GIDistance",&Settings::giDistance,36,84,{76,52,52}},
    {"GIProbeAhead",&Settings::giProbeAhead,0,48,{0,0,0}},
    {"HorizonHaze",&Settings::horizonHaze,0,100,{50,50,50}},
    {"HorizonHazeStart",&Settings::horizonHazeStart,50,95,{75,75,75}},
    {"HorizonHazeBand",&Settings::horizonHazeBand,2,15,{6,6,6}},
    {"HorizonHazeTerrain",&Settings::horizonHazeTerrain,0,1,{1,1,1}},
    {"ActorShadows",&Settings::actorShadows,0,1,{1,1,1}},
    {"FrameDrawGates",&Settings::frameDrawGates,0,1,{1,1,1}},
    {"TranslucentActorDepth",&Settings::translucentActorDepth,0,2,{1,1,1}},
};
inline bool operator==(const Settings& a,const Settings& b){for(const auto& k:Keys)if(a.*k.field!=b.*k.field)return false;return true;}
inline bool operator!=(const Settings& a,const Settings& b){return !(a==b);}
// Probe work actually requested: GI=0 also removes actor capture and dynamic probes.
inline bool giActorCapture(const Settings& s){return s.gi&&s.giDynamicProbes;}
// F8 can only hide GI; GI=0 keeps the pass off whatever F8 requests.
inline bool giPass(const Settings& s,bool requested){return requested&&s.gi;}
// Solver threads actually used: GIThreads, never more than cores-1 (the game
// thread keeps a core); 0 cores (unknown) means 1. 1 = the original serial loop.
inline unsigned giSolverThreads(const Settings& s,unsigned cores){
    if(!s.gi)return 0;const unsigned limit=cores>1?cores-1:1;return s.giThreads<limit?s.giThreads:limit;}
// Effective fate tracker: Diagnostics=0 wins over ShadowFateDiagnostics=1.
inline bool shadowFate(const Settings& s){return s.diagnostics&&s.shadowFateDiagnostics;}
// Effective 0.3.149 instrumentation: Diagnostics=0 wins; the probe needs RenderProfile.
inline bool renderProfile(const Settings& s){return s.diagnostics&&s.renderProfile;}
inline bool replayProbe(const Settings& s){return renderProfile(s)&&s.diagReplayProbe;}
// Horizontal probe window cells for GIDistance: d = 4N-4 with N even, snapped down
// (36..84 -> 10..22; 52 -> 14). The vertical extent stays 14 cells.
inline unsigned giProbeGrid(const Settings& s){
    const unsigned d=s.giDistance<36?36:s.giDistance>84?84:s.giDistance;return 2*((d+4)/8);}
// Shader GI intensity (GridInfo.y): GIStrength=60 gives exactly the 0.3.141 constant 0.5.
inline float giIntensity(const Settings& s){return .5f*float(s.giStrength)/60.f;}
// 0.3.158 ActorShadows=0: keys that only act on replay (actor) shadows are forced to 0.
struct ForcedKey { const char* name; unsigned Settings::*field; };
inline const ForcedKey ActorShadowForced[]={
    {"ShadowFateDiagnostics",&Settings::shadowFateDiagnostics},{"DiagReplayProbe",&Settings::diagReplayProbe}};
// Keys of earlier versions that no longer exist: a line setting one is ignored with a
// "retired" note instead of "unknown key". The persistent casters (PersistentCasters,
// PersistentRigidProps): rigid_memory.h replaces the rigid-prop part.
struct RetiredKey { const char* name; const char* version; };
inline const RetiredKey Retired[]={{"PersistentCasters","0.3.172"},{"PersistentRigidProps","0.3.172"}};
inline Settings effective(Settings s){if(!s.actorShadows)for(const auto& k:ActorShadowForced)s.*k.field=0;return s;}
// Names of the keys effective() turned off (requested non-zero), space separated; "none" if nothing.
inline std::string forcedOff(const Settings& requested){
    std::string out;const Settings e=effective(requested);
    for(const auto& k:ActorShadowForced)if(requested.*k.field&&!(e.*k.field)){if(!out.empty())out+=' ';out+=k.name;}
    return out.empty()?"none":out;}
// Replay shadow work actually requested: shadows on (F9) and ActorShadows=1.
inline bool actorShadowWork(const Settings& s,bool shadows){return shadows&&s.actorShadows;}
inline const char* name(Preset p){return p==Preset::Balanced?"Balanced":p==Preset::Performance?"Performance":"Quality";}
static_assert(sizeof(Keys)/sizeof(Keys[0])==sizeof(Settings::origin),"one origin per key");
inline Settings preset(Preset p){Settings s;s.preset=p;unsigned i=0;
    for(const auto& k:Keys){s.*k.field=k.preset[unsigned(p)];s.origin[i++]=p==Preset::Quality?'d':'p';}return s;}
inline std::string lower(std::string s){for(auto& c:s)c=char(std::tolower((unsigned char)c));return s;}
inline std::string trim(const std::string& s){
    size_t a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");
    return a==std::string::npos?std::string():s.substr(a,b-a+1);
}
// Decimal digits only (GetPrivateProfileInt-style "50abc" or "-1" is rejected).
inline bool number(const std::string& text,unsigned& out){
    if(text.empty()||text.size()>6||text.find_first_not_of("0123456789")!=std::string::npos)return false;
    out=unsigned(std::strtoul(text.c_str(),nullptr,10));return true;
}
// Explicit key values of one INI section, in file order. Unrecognised
// sections are skipped; comments start with ';' or '#', also after a value.
struct Entry { std::string key,value;unsigned line=0; };
inline std::vector<Entry> section(std::istream& in,const char* wanted,std::vector<std::string>& problems){
    std::vector<Entry> out;std::string line,current;unsigned n=0;const std::string target=lower(wanted);
    while(std::getline(in,line)&&n<4096){++n;
        if(n==1&&line.size()>=3&&line.compare(0,3,"\xEF\xBB\xBF")==0)line.erase(0,3); /* Notepad UTF-8 BOM */
        line=trim(line.substr(0,line.find_first_of(";#")));if(line.empty())continue;
        if(line.front()=='['){
            if(line.back()!=']'){problems.push_back("line "+std::to_string(n)+": malformed section ignored");current.clear();continue;}
            current=lower(trim(line.substr(1,line.size()-2)));continue;
        }
        if(current!=target)continue;
        const size_t eq=line.find('=');
        if(eq==std::string::npos){problems.push_back("line "+std::to_string(n)+": expected Key=Value");continue;}
        out.push_back({trim(line.substr(0,eq)),trim(line.substr(eq+1)),n});
    }
    return out;
}
// Apply explicit keys over `s`. Invalid or out-of-range values keep the value
// already in `s` (the preset or legacy value) and are reported, never guessed.
inline void apply(const std::vector<Entry>& entries,Settings& s,const char* source,char origin,std::vector<std::string>& problems,bool allowPreset){
    for(const auto& e:entries){
        const std::string key=lower(e.key);const std::string where=std::string(source)+" line "+std::to_string(e.line)+": ";
        if(key=="preset"){if(!allowPreset)problems.push_back(where+"Preset ignored here");continue;}
        const Key* match=nullptr;unsigned index=0;for(unsigned i=0;i<sizeof(Keys)/sizeof(Keys[0]);++i)if(lower(Keys[i].name)==key){match=&Keys[i];index=i;}
        const RetiredKey* retired=nullptr;for(const auto& r:Retired)if(lower(r.name)==key)retired=&r;
        if(retired){problems.push_back(where+retired->name+" retired in "+retired->version+" (ignored)");continue;}
        if(!match){problems.push_back(where+"unknown key "+e.key+" ignored");continue;}
        unsigned v=0;
        if(!number(e.value,v)||v<match->low||v>match->high){
            problems.push_back(where+e.key+"="+e.value+" invalid (allowed "+std::to_string(match->low)+".."+std::to_string(match->high)+"); kept "+std::to_string(s.*match->field));continue;}
        s.*match->field=v;s.origin[index]=origin;
    }
}
inline Preset presetOf(const std::vector<Entry>& entries,std::vector<std::string>& problems){
    Preset p=Preset::Quality;
    for(const auto& e:entries){if(lower(e.key)!="preset")continue;const std::string v=lower(e.value);
        if(v=="quality")p=Preset::Quality;else if(v=="balanced")p=Preset::Balanced;else if(v=="performance")p=Preset::Performance;
        else problems.push_back("northlight-quality.ini line "+std::to_string(e.line)+": Preset="+e.value+" unknown; using "+name(p));}
    return p;
}
// Notepad "Unicode" (UTF-16LE with BOM) -> bytes; the keys and values are ASCII.
inline bool bigEndianUtf16(const std::string& raw){return raw.size()>=2&&(unsigned char)raw[0]==0xFE&&(unsigned char)raw[1]==0xFF;}
inline std::string narrow(const std::string& raw){
    if(raw.size()<2||(unsigned char)raw[0]!=0xFF||(unsigned char)raw[1]!=0xFE)return raw;
    std::string out;for(size_t i=2;i+1<raw.size();i+=2){const unsigned c=(unsigned char)raw[i]|unsigned((unsigned char)raw[i+1])<<8;out.push_back(c<128?char(c):'?');}
    return out;
}
// Precedence:
//   Quality (or no file): code defaults < legacy shadow-experiment.ini, i.e. each machine exactly as 0.3.136.
//   Balanced/Performance: the preset's values win over the legacy file (it is not applied).
//   Explicit northlight-quality.ini keys win over everything.
// Either stream may be null (file absent). Both absent gives Settings{}.
inline Settings load(std::istream* quality,std::istream* legacy,std::vector<std::string>& problems){
    std::vector<Entry> own;if(quality)own=section(*quality,"Quality",problems);
    Settings s=preset(presetOf(own,problems));
    if(legacy&&s.preset!=Preset::Quality)problems.push_back(std::string("shadow-experiment.ini not applied: Preset=")+name(s.preset)+" sets these values");
    else if(legacy){auto old=section(*legacy,"ShadowExperiment",problems);
        // The legacy file only ever held these three keys.
        std::vector<Entry> kept;for(auto& e:old){const auto k=lower(e.key);
            if(k=="minskinnedtriangles"||k=="capturebudgetmib"||k=="actorshadowbudgetmib")kept.push_back(e);
            else problems.push_back("shadow-experiment.ini line "+std::to_string(e.line)+": unknown key "+e.key+" ignored");}
        apply(kept,s,"shadow-experiment.ini",'l',problems,false);}
    apply(own,s,"northlight-quality.ini",'f',problems,true);
    return s;
}
// Far/NearShadowInterval>1: reuse of one source's last COMPLETE cascade map with the
// matrix it was rendered with. begin() is called before a render writes anything,
// so a render that fails part-way is never reused; commit() only after success.
struct ShadowMapReuse {
    float matrix[16]={};unsigned pass=0;bool valid=false;
    void invalidate(){valid=false;}
    /* Reusable at pass `now` if nothing else forces a render (the capture-skip prediction). */
    bool fresh(unsigned interval,unsigned now)const{return interval>1&&valid&&now-pass<interval;}
    bool canSkip(unsigned interval,unsigned now,bool cacheReusable,bool diagnostic)const{
        return cacheReusable&&!diagnostic&&fresh(interval,now);}
    void begin(unsigned interval){if(interval>1)valid=false;}
    void commit(unsigned interval,unsigned now,const float* rendered){
        if(interval<=1)return;for(int i=0;i<16;++i)matrix[i]=rendered[i];pass=now;valid=true;}
};
using FarShadowReuse=ShadowMapReuse;
// One cascade on one frame. Defer: this frame's model capture was skipped, so a
// complete map is kept one frame past its schedule (the caller demands a capture);
// Render on such a frame draws no replays and must not commit().
enum class CascadeAction {Render,Reuse,Defer};
inline CascadeAction cascadeAction(const ShadowMapReuse& r,unsigned interval,unsigned now,bool cacheReusable,bool diagnostic,bool freshReplays){
    if(r.canSkip(interval,now,cacheReusable,diagnostic))return CascadeAction::Reuse;
    return !freshReplays&&interval>1&&r.valid&&!diagnostic?CascadeAction::Defer:CascadeAction::Render;
}
// Alignment: a far map that would be over age at the next near render is refreshed
// now, on this capture frame, instead of forcing a capture of its own later (e.g.
// Near 2 / Far 3 renders far every 2). Only ever fresher; never with Near 1.
inline bool pullFar(unsigned nearInterval,unsigned farInterval,unsigned now,const ShadowMapReuse& farMap,bool nearRenderedFresh){
    return nearRenderedFresh&&nearInterval>1&&farMap.valid&&now-farMap.pass+nearInterval>farInterval;}
// Model (replay) capture is needed on a frame only by a consumer that draws or
// packs fresh replays: GI actor packets (every ~200 ms), a directional cascade
// that cannot reuse its complete map, a due point-shadow refresh, a demand left
// by a previous skipped frame, or a GPU diagnostic dump. Shadows off: GI only.
// Shadows on: only when BOTH intervals are >1 (1/1 is exactly the old path).
// A wrong prediction is safe: that frame keeps its complete maps one more frame
// and demands a capture (see WorldRenderer::render).
struct CaptureInputs {
    bool shadows=true,actorDue=false,demand=false,diagnostic=false,pointDue=false;
    bool sourceActive[2]={};unsigned nextPass=0;
};
inline bool captureSkipPossible(const Settings& s,bool shadows){return !shadows||(s.nearShadowInterval>1&&s.farShadowInterval>1);}
inline bool skipModelCapture(const Settings& s,const CaptureInputs& in,const ShadowMapReuse* nearMaps,const ShadowMapReuse* farMaps){
    if(!captureSkipPossible(s,in.shadows)||in.actorDue||in.demand||in.diagnostic)return false;
    if(!in.shadows)return true;
    if(in.pointDue)return false;
    for(int source=0;source<2;++source)if(in.sourceActive[source]&&
        (!nearMaps[source].fresh(s.nearShadowInterval,in.nextPass)||!farMaps[source].fresh(s.farShadowInterval,in.nextPass)))return false;
    return true;
}
inline std::string describe(const Settings& s){
    std::ostringstream o;o<<"preset="<<name(s.preset);
    unsigned i=0;for(const auto& k:Keys){const char c=s.origin[i++];
        o<<' '<<k.name<<'='<<s.*k.field<<'('<<(c=='f'?"file":c=='l'?"legacy":c=='p'?"preset":"default")<<')';}
    o<<" shadowFateEffective="<<(shadowFate(s)?1:0)<<" renderProfileEffective="<<(renderProfile(s)?1:0)<<" replayProbeEffective="<<(replayProbe(s)?1:0)<<" codeDefaults="<<(s==Settings{}?1:0);
    return o.str();
}
} // namespace NorthlightQuality
