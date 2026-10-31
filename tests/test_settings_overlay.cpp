// Settings overlay logic without Windows, D3D9 or ImGui (tests/test_settings_overlay.py): the window procedure's
// routing decisions (overlay_input.h) and the edit draft's value rules (settings_draft.h).
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "overlay_input.h"
#include "settings_draft.h"

static int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);++failures;}}while(0)

namespace In=NorthlightOverlayInput;
namespace D=NorthlightSettingsDraft;
using NorthlightSettings::Field;

static In::Decision route(unsigned msg,In::State& s,std::uintptr_t w=0,std::intptr_t l=0,In::Modifiers m={}){return In::route(msg,w,l,m,s);}

static void routing(){
    In::Modifiers hot;hot.ctrl=hot.shift=true;
    {   // Closed: everything reaches the game and nothing is queued.
        In::State s;
        for(unsigned msg:{In::Msg::MouseMove,In::Msg::LButtonDown,In::Msg::LButtonUp,In::Msg::MouseWheel,In::Msg::KeyDown,In::Msg::KeyUp,In::Msg::Char}){
            const auto d=route(msg,s,'A');CHECK(!d.swallow&&!d.queue&&!d.toggle);}
    }
    {   // The hotkey toggles once per press and never reaches the game; its release and plain F11 do.
        In::State s;
        auto d=route(In::Msg::KeyDown,s,In::VkF11,0,hot);CHECK(d.toggle&&d.swallow);
        d=route(In::Msg::KeyDown,s,In::VkF11,std::intptr_t(1)<<30,hot);CHECK(!d.toggle&&d.swallow);   // auto-repeat
        d=route(In::Msg::SysKeyDown,s,In::VkF11,0,hot);CHECK(d.toggle&&d.swallow);
        d=route(In::Msg::KeyUp,s,In::VkF11,0,hot);CHECK(!d.swallow);
        In::Modifiers ctrl;ctrl.ctrl=true;
        d=route(In::Msg::KeyDown,s,In::VkF11,0,ctrl);CHECK(!d.toggle&&!d.swallow);
        d=route(In::Msg::KeyDown,s,In::VkF11);CHECK(!d.toggle&&!d.swallow);
    }
    {   // Open, cursor away from the window: ImGui sees input, the game keeps it.
        In::State s;s.open=true;
        for(unsigned msg:{In::Msg::MouseMove,In::Msg::LButtonDown,In::Msg::RButtonDown,In::Msg::MouseWheel,In::Msg::KeyDown,In::Msg::KeyUp,In::Msg::Char}){
            const auto d=route(msg,s);CHECK(d.queue&&!d.swallow);}
        CHECK(s.heldButtons==0);
        const auto up=route(In::Msg::LButtonUp,s);CHECK(up.queue&&!up.swallow);
    }
    {   // Open, cursor over the window: presses and the wheel are ImGui's; moves and key-ups still reach the game.
        In::State s;s.open=true;s.captureMouse=true;
        auto d=route(In::Msg::LButtonDown,s);CHECK(d.queue&&d.swallow&&s.heldButtons==1u);
        CHECK(!route(In::Msg::MouseMove,s).swallow);
        CHECK(route(In::Msg::MouseWheel,s).swallow&&route(In::Msg::MouseHWheel,s).swallow);
        CHECK(!route(In::Msg::KeyUp,s).swallow&&!route(In::Msg::SysKeyUp,s).swallow);
        CHECK(!route(In::Msg::KeyDown,s,'W').swallow);   // no text field: movement keys keep working
        // The release of an ImGui press stays with ImGui even after the window closed (the game never saw the press).
        s.open=false;s.captureMouse=false;
        d=route(In::Msg::LButtonUp,s);CHECK(d.swallow&&d.queue&&s.heldButtons==0);
        d=route(In::Msg::LButtonUp,s);CHECK(!d.swallow&&!d.queue);
    }
    {   // A drag that began in the game ends in the game even if the cursor is now over the window.
        In::State s;s.open=true;
        CHECK(!route(In::Msg::RButtonDown,s).swallow);
        s.captureMouse=true;
        CHECK(!route(In::Msg::RButtonUp,s).swallow);
    }
    {   // X buttons map to their own bits.
        In::State s;s.open=true;s.captureMouse=true;
        CHECK(In::button(In::Msg::XButtonDown,std::uintptr_t(2)<<16)==4&&In::button(In::Msg::XButtonDown,std::uintptr_t(1)<<16)==3);
        route(In::Msg::XButtonDown,s,std::uintptr_t(2)<<16);CHECK(s.heldButtons==(1u<<4));
        CHECK(route(In::Msg::XButtonUp,s,std::uintptr_t(2)<<16).swallow&&s.heldButtons==0);
    }
    {   // A text field takes key downs and characters; key-ups still pass.
        In::State s;s.open=true;s.captureText=true;
        CHECK(route(In::Msg::KeyDown,s,'A').swallow&&route(In::Msg::Char,s,'a').swallow&&route(In::Msg::SysKeyDown,s,'A').swallow);
        CHECK(!route(In::Msg::KeyUp,s,'A').swallow);
        CHECK(route(In::Msg::KillFocus,s).queue&&!route(In::Msg::KillFocus,s).swallow);
        CHECK(route(In::Msg::ActivateApp,s,0).queue&&!route(In::Msg::ActivateApp,s,1).queue);
    }
    {   // Stuck release: a press goes to the panel, focus is lost, the panel closes; the game's next press and release both reach it.
        In::State s;s.open=true;s.captureMouse=true;s.self=0x1234;
        CHECK(route(In::Msg::LButtonDown,s).swallow&&s.heldButtons==1u);
        const auto lost=route(In::Msg::KillFocus,s);CHECK(lost.lost&&lost.queue&&!lost.swallow&&s.heldButtons==0);
        s.open=false;s.captureMouse=false;
        CHECK(!route(In::Msg::LButtonDown,s).swallow&&!route(In::Msg::LButtonUp,s).swallow);
        // The same with the window closed by the hotkey, and with capture or activation lost.
        s.open=true;s.captureMouse=true;route(In::Msg::RButtonDown,s);
        In::Modifiers hot2;hot2.ctrl=hot2.shift=true;CHECK(route(In::Msg::KeyDown,s,In::VkF11,0,hot2).toggle&&s.heldButtons==0);
        s.open=true;route(In::Msg::MButtonDown,s);CHECK(route(In::Msg::CaptureChanged,s).lost&&s.heldButtons==0);
        route(In::Msg::MButtonDown,s);route(In::Msg::ActivateApp,s,0);CHECK(s.heldButtons==0);
        s.open=false;CHECK(route(In::Msg::KillFocus,s).lost&&!route(In::Msg::KillFocus,s).queue);   // closed: forgotten, not queued
        // A press that reaches the game clears a stale mark, so its release reaches the game too.
        s.open=true;s.captureMouse=false;s.heldButtons=1u;
        CHECK(!route(In::Msg::LButtonDown,s).swallow&&s.heldButtons==0&&!route(In::Msg::LButtonUp,s).swallow);
    }
    {   // Mouse capture: taken for the first overlay press, kept while overlay buttons are held, given back with the last release.
        In::State s;s.open=true;s.captureMouse=true;s.self=0x1234;
        auto d=route(In::Msg::LButtonDown,s);CHECK(d.swallow&&d.capture&&s.captured);
        // Our own SetCapture re-enters with WM_CAPTURECHANGED naming this window: nothing changes.
        d=route(In::Msg::CaptureChanged,s,0,0x1234);CHECK(!d.lost&&!d.queue&&s.captured&&s.heldButtons==1u);
        d=route(In::Msg::RButtonDown,s);CHECK(d.swallow&&!d.capture&&s.heldButtons==3u);
        s.captureMouse=false;   // the cursor left the window mid-drag: the releases still belong to the overlay
        d=route(In::Msg::LButtonUp,s);CHECK(d.swallow&&!d.release&&s.captured);
        d=route(In::Msg::RButtonUp,s);CHECK(d.swallow&&d.release&&!s.captured&&s.heldButtons==0);
        // Our own ReleaseCapture re-enters with WM_CAPTURECHANGED (to nobody): nothing is held, nothing changes.
        d=route(In::Msg::CaptureChanged,s,0,0);CHECK(!d.lost&&!d.queue&&!d.release);
        // The game's presses are never captured by us.
        d=route(In::Msg::LButtonDown,s);CHECK(!d.swallow&&!d.capture&&!s.captured);
        CHECK(!route(In::Msg::LButtonUp,s).swallow);
        // Another window takes the capture mid-drag: held state cleared, ImGui told, no ReleaseCapture of a capture we lost.
        s.captureMouse=true;route(In::Msg::LButtonDown,s);
        d=route(In::Msg::CaptureChanged,s,0,0x9999);CHECK(d.lost&&d.queue&&!d.release&&!s.captured&&s.heldButtons==0);
        CHECK(!route(In::Msg::LButtonUp,s).swallow);
        // Focus loss and the hotkey give a capture back.
        route(In::Msg::LButtonDown,s);CHECK(s.captured);
        d=route(In::Msg::KillFocus,s);CHECK(d.lost&&d.release&&!s.captured);
        route(In::Msg::LButtonDown,s);In::Modifiers h;h.ctrl=h.shift=true;
        d=route(In::Msg::KeyDown,s,In::VkF11,0,h);CHECK(d.toggle&&d.release&&!s.captured&&s.heldButtons==0);
        // A press that could not be queued goes to the game instead: no mark, no capture.
        s.open=true;s.captureMouse=true;d=route(In::Msg::LButtonDown,s);In::dropped(In::Msg::LButtonDown,0,d,s);
        CHECK(!d.swallow&&!d.capture&&!s.captured&&s.heldButtons==0);
    }
    {   // Queue overflow: moves coalesce, releasing events survive a full queue, anything dropped raises the flag once.
        In::Queue q;In::Event move;move.msg=In::Msg::MouseMove;
        for(int i=0;i<1000;++i){move.lparam=i;CHECK(q.push(move));}
        CHECK(q.size()==1);
        In::Event up;up.msg=In::Msg::LButtonUp;CHECK(q.push(up));
        std::vector<In::Event> out;CHECK(!q.drain(out));
        CHECK(out.size()==2&&out[0].msg==In::Msg::MouseMove&&out[0].lparam==999&&out[1].msg==In::Msg::LButtonUp);
        In::Event down;down.msg=In::Msg::LButtonDown;
        for(std::size_t i=0;i<In::Queue::Capacity;++i)CHECK(q.push(down));
        CHECK(!q.push(down));                               // a press is dropped when full
        In::Event key;key.msg=In::Msg::KeyUp;CHECK(q.push(up)&&q.push(key));   // releases still fit
        for(std::size_t i=0;i<In::Queue::Reserve;++i){if(!q.push(up))break;}
        CHECK(!q.push(up));                                 // the reserve is bounded too
        CHECK(q.drain(out)&&out.size()==In::Queue::Capacity+In::Queue::Reserve&&out[In::Queue::Capacity].msg==In::Msg::LButtonUp);
        CHECK(q.push(down)&&!q.drain(out));                 // the flag is consumed by the drain that reports it
    }
    {   // SettingsOverlayKey.
        In::Hotkey k;
        CHECK(In::parseHotkey("Ctrl+Shift+F11",k)&&k==In::Hotkey{}&&In::hotkeyName(k)=="Ctrl+Shift+F11");
        CHECK(In::parseHotkey(" alt + f5 ",k)&&k.vk==In::VkF1+4&&k.mods.alt&&!k.mods.ctrl&&!k.mods.shift&&In::hotkeyName(k)=="Alt+F5");
        CHECK(In::parseHotkey("Control+F1",k)&&k.vk==In::VkF1&&k.mods.ctrl);
        CHECK(In::parseHotkey("Ctrl+Shift+Alt+F12",k)&&k.vk==In::VkF1+11);
        for(const char* bad:{"","Ctrl+Shift","F13","F0","Ctrl+Ctrl+F2","Ctrl+X","Ctrl++F2","Ctrl+Shift+F7","Ctrl+Shift+F10","Ctrl+Shift+F12","F1+F2"}){
            In::Hotkey before;CHECK(!In::parseHotkey(bad,before)&&before==In::Hotkey{});}
        In::State s;CHECK(In::parseHotkey("Alt+F5",s.hotkey));
        In::Modifiers alt;alt.alt=true;In::Modifiers hot2;hot2.ctrl=hot2.shift=true;
        CHECK(route(In::Msg::SysKeyDown,s,In::VkF1+4,0,alt).toggle);
        CHECK(!route(In::Msg::KeyDown,s,In::VkF11,0,hot2).swallow);   // the default no longer toggles
        In::Modifiers altCtrl=alt;altCtrl.ctrl=true;CHECK(!route(In::Msg::SysKeyDown,s,In::VkF1+4,0,altCtrl).toggle);   // modifiers match exactly
    }
    // Client pixels scale to the back buffer.
    CHECK(In::mapCoordinate(960,1920,3840)==1920.f&&In::mapCoordinate(10,0,100)==10.f);
    {   // The queue is bounded.
        In::Queue q;In::Event e;std::size_t accepted=0;
        for(std::size_t i=0;i<In::Queue::Capacity+10;++i)accepted+=q.push(e)?1:0;
        CHECK(accepted==In::Queue::Capacity);std::vector<In::Event> out;CHECK(q.drain(out));CHECK(out.size()==In::Queue::Capacity&&q.size()==0);
    }
}

static Field field(unsigned file,const char* section,const char* key,const char* kind,const char* options,const char* inherited,const char* override_="~",const char* apply="F"){
    Field f;f.file=file;f.section=section;f.key=key;f.kind=kind;f.options=options;f.inherited=inherited;f.overrideValue=override_;f.apply=apply;
    f.saved=f.active=f.factory=override_[0]=='~'?inherited:override_;f.low="0";f.high=std::string(kind)=="rgb"?"4":"100";return f;
}

static void draft(){
    std::vector<Field> fields={
        field(D::Quality,"Quality","Preset","enum","Quality,Balanced,Performance","Quality"),
        field(D::Quality,"Quality","GI","check","1,1,0","1"),
        field(D::Quality,"Quality","GIStrength","slider","100,80,60","100"),
        field(D::Quality,"Quality","AOMethod","enum","SSAO,HDAO,HBAOPlus,GTAO","SSAO"),
        field(D::Quality,"Quality","AutoBumpTerrain","slider","2,1,0","2"),
        field(D::Quality,"Quality","CommandStream","check","1,1,1","1","~","R"),
        field(D::Renderer,"Renderer","Backend","enum","native,dxvk","legacy","dxvk","R"),
        field(D::Renderer,"Renderer","ExperimentalHDR","check","","0","~","R"),
        field(D::Renderer,"Renderer","HDRExposure","slider","","0"),
        field(D::Celestial,"default","sun_disc","rgb","","1,1,1","1,0.9,0.8"),
        field(D::Celestial,"Azeroth:12","sun_disc","rgb","","1,0.9,0.8"),
        field(D::ShadowRange,"Azeroth:12","terrain_radius","slider","","1000","1500"),
    };
    fields[4].high="2";fields[1].high="1";
    D::Draft d;d.bind(&fields);
    const Field &preset=fields[0],&gi=fields[1],&strength=fields[2],&ao=fields[3],&bump=fields[4],&stream=fields[5],&backend=fields[6],&hdr=fields[7],&exposure=fields[8],
        &sunDefault=fields[9],&sunRegion=fields[10],&range=fields[11];
    // Quality keys follow the (drafted) preset.
    CHECK(d.effective(strength)=="100"&&d.effective(gi)=="1");
    d.set(preset,"Performance");CHECK(d.edited(preset)&&d.effective(strength)=="60"&&d.effective(gi)=="0"&&d.effective(ao)=="SSAO");
    d.set(strength,"60");CHECK(!d.edited(strength));            // equal to what it inherits: no entry
    d.set(strength,"70");CHECK(d.edited(strength)&&d.effective(strength)=="70"&&d.overrides(strength));
    d.set(strength,"60");CHECK(!d.edited(strength));            // back to the baseline removes the edit
    d.set(preset,"Quality");CHECK(!d.edited(preset)&&d.empty());
    // Mode fields read as Off / Normal maps / Normal maps + POM.
    CHECK(D::modeField(bump)&&D::modeOptions(bump).size()==3&&!D::modeField(strength));
    auto model=bump;model.key="AutoBumpModels";model.high="3";
    CHECK(D::modeField(model)&&D::modeOptions(model)==std::vector<std::string>({"0","1","2","3"}));
    model.key="AutoBumpGrass";model.high="1";CHECK(D::modeOptions(model).size()==2);
    // Regional Sun/Moon keys inherit the default profile, including its draft.
    CHECK(d.effective(sunRegion)=="1,0.9,0.8"&&!d.overrides(sunRegion)&&d.overrides(sunDefault));
    d.set(sunDefault,"0.5,0.5,0.5");CHECK(d.effective(sunRegion)=="0.5,0.5,0.5");
    d.set(sunRegion,"0.50,0.5,0.500");CHECK(!d.edited(sunRegion));   // numerically the same
    // Right-click: a saved override becomes "~", an inherited field needs no entry.
    d.inherit(sunDefault);CHECK(d.edited(sunDefault)&&d.effective(sunDefault)=="1,1,1"&&!d.overrides(sunDefault));
    d.inherit(strength);CHECK(!d.edited(strength));
    d.factory(range);CHECK(d.edited(range));
    // Dependencies and availability.
    std::string missing;
    CHECK(D::blocked(d,exposure,missing)==D::Block::NeedsHdr);
    d.set(hdr,"1");CHECK(D::blocked(d,exposure,missing)==D::Block::None);
    CHECK(D::blocked(d,strength,missing)==D::Block::None);
    d.set(gi,"0");CHECK(D::blocked(d,strength,missing)==D::Block::NeedsKey&&missing=="GI");
    Field gone=strength;gone.available=false;CHECK(D::blocked(d,gone,missing)==D::Block::Unavailable);
    CHECK(!D::canInherit(backend));                          // no legacy backend installed: Backend cannot be removed
    // Restart-only keys whose shown value differs from the running one.
    CHECK(d.restartPending(hdr)&&!d.restartPending(exposure)&&!d.restartPending(stream));
    // Changes go out as id/value pairs; a new snapshot without a field drops its edit.
    const auto changes=d.changes();bool sawHdr=false;for(const auto& c:changes)if(c.first==hdr.id())sawHdr=c.second=="1";CHECK(sawHdr);
    std::vector<Field> fewer(fields.begin(),fields.begin()+8);d.bind(&fewer);
    for(const auto& c:d.changes())CHECK(c.first!=sunDefault.id()&&c.first!=range.id());
    CHECK(D::same(strength,"1.50","1.5")&&!D::same(ao,"SSAO","HDAO")&&!D::same(strength,"1,2","1"));
}

int main(){
    routing();draft();
    if(failures){std::fprintf(stderr,"%d failure(s)\n",failures);return 1;}
    std::puts("settings overlay: routing and draft OK");
    return 0;
}
