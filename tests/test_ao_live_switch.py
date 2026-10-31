# northlight-test: requires=cxx
"""Exercise the production resource path and frame-boundary AO switch with observed lifetimes."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source = fp.src('renderer.cpp').read_text(encoding='utf-8')

def extract(marker):
    start = source.index(marker)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

# HDR must request only the resources that its color conversion consumes.
assert 'resources(desc.Width,desc.Height,desc.Format,true)' in re.sub(r'\s+', '', extract('    HRESULT hdrBegin('))
live_settings = extract('if(settingsService)if(auto update=settingsService->after(settingsRevision))')
ao_update = re.search(r'selectAO\(world->aoMethod\(\),world->aoQuality\(\)\);\s*contactAO=world->contactAO\(\);aoStrength=world->aoStrength\(\);aoRadius=world->aoRadius\(\);', live_settings)[0]
assert live_settings.index('world->applyLiveSettings(update->config)') < live_settings.index(ao_update) < live_settings.index('settingsRevision=update->revision')

harness = r'''
#include "quality_settings.h"
#include <cassert>
#include <iostream>
#include <vector>
using UINT=unsigned;using DWORD=unsigned;using HRESULT=int;using D3DFORMAT=unsigned;
constexpr unsigned MAKEFOURCC(char a,char b,char c,char d){return unsigned(a)|(unsigned(b)<<8)|(unsigned(c)<<16)|(unsigned(d)<<24);}
constexpr unsigned D3DFMT_UNKNOWN=0,D3DFMT_A16B16G16R16F=1,D3DUSAGE_RENDERTARGET=1,D3DUSAGE_DEPTHSTENCIL=2,D3DPOOL_DEFAULT=0,D3DMULTISAMPLE_NONE=0;
bool FAILED(int hr){return hr<0;}
namespace NorthlightBackend {bool isPackagedDxvk(int backend){return backend==1;}}
const DWORD kAoShader[]={1},kAoContactBloomShader[]={2},kHdaoContactBloomShader[]={3},kHbaoPlusContactBloomShader[]={4},kGtaoContactBloomShader[]={5},kAoBlurShader[]={6},kContactBloomShader[]={7},kCompositeShader[]={8};
const DWORD kHdaoContactBloomMediumShader[]={9},kHbaoPlusContactBloomMediumShader[]={10},kGtaoContactBloomMediumShader[]={11},kHdaoContactBloomLowShader[]={12},kHbaoPlusContactBloomLowShader[]={13},kGtaoContactBloomLowShader[]={14};
unsigned alive=0;std::size_t textureBytes=0;
struct Surface {Surface(){++alive;}void Release(){--alive;delete this;}};
struct Texture {
    unsigned id;std::size_t bytes;bool* failSurface;
    Texture(unsigned serial,unsigned w,unsigned h,unsigned format,bool* fail):id(serial),bytes(std::size_t(w)*h*(format==D3DFMT_A16B16G16R16F?8:4)),failSurface(fail){++alive;textureBytes+=bytes;}
    void Release(){--alive;textureBytes-=bytes;delete this;}
    int GetSurfaceLevel(unsigned,Surface** out){if(*failSurface)return -1;*out=new Surface;return 0;}
};
struct Shader {const DWORD* code;Shader(const DWORD* p):code(p){++alive;}void Release(){--alive;delete this;}};
template<class T>void drop(T*& p){if(p){p->Release();p=nullptr;}}
struct Device {
    unsigned textures=0,shaders=0,failTextureAt=0;bool failShader=false,failSurface=false,failDF24=false;
    struct Allocation {unsigned w,h,usage,format;};std::vector<Allocation> allocations;
    int CreatePixelShader(const DWORD* code,Shader** out){if(failShader)return -1;++shaders;*out=new Shader(code);return 0;}
    int CreateTexture(unsigned w,unsigned h,unsigned,unsigned usage,unsigned format,unsigned,Texture** out,void*){
        ++textures;allocations.push_back({w,h,usage,format});
        if(textures==failTextureAt||(failDF24&&format==MAKEFOURCC('D','F','2','4')))return -1;
        *out=new Texture(textures,w,h,format,&failSurface);return 0;
    }
};
struct Service {};
struct World {unsigned method=0,quality=0;bool enabled=true;unsigned aoMethod()const{return method;}unsigned aoQuality()const{return quality;}bool contactAO()const{return enabled;}float aoStrength()const{return 1.f;}float aoRadius()const{return 2.f;}};
struct Renderer {
    Device* ext;Service* settingsService;World* world;
    unsigned width=0,height=0,aoMethod=0,aoQuality=0,selectedBackend=0;
    float aoStrength=0,aoRadius=0;
    unsigned sceneFormat=0,depthSnapshotFormat=0;
    bool worldDepthDescKnown=false,df24Refused=false,rainDepthOk=false,rainMaskCleared=false,rainMaskDrawn=false,rainMaskFailed=false,captured=false,contactAO=true,failed=false;
    struct {unsigned MultiSampleType=0;}worldDepthDesc;
    Surface *sceneSurface=nullptr,*aoSurface=nullptr,*aoRawSurface=nullptr,*rainMaskMS=nullptr,*rainMaskSurface=nullptr;
    Texture *scene=nullptr,*ao=nullptr,*aoRaw=nullptr,*depthTex=nullptr,*rainDepth=nullptr,*rainMask=nullptr;
    Shader *aoPS=nullptr,*aoContactBloomPS=nullptr,*aoBlurPS=nullptr,*contactBloomPS=nullptr,*compositePS=nullptr;
    bool error(int hr,const char*){if(hr<0)failed=true;return hr<0;}
    void rainMrtUnbind(){}
    template<class...T>void logf(const char*,T...){}
    __SELECT__
    __RESOURCES__
    void nextFrame(){__FRAME__}
    ~Renderer(){drop(sceneSurface);drop(aoSurface);drop(aoRawSurface);drop(scene);drop(ao);drop(aoRaw);drop(depthTex);drop(rainMaskMS);drop(rainMaskSurface);drop(rainMask);drop(rainDepth);drop(aoPS);drop(aoContactBloomPS);drop(aoBlurPS);drop(contactBloomPS);drop(compositePS);}
};
int main(){
    {
        Device d;Renderer r;r.ext=&d;
        assert(r.resources(3840,2160,10,true));
        assert(d.textures==1&&d.shaders==0&&textureBytes==33177600);
        assert(r.scene&&r.sceneSurface&&!r.depthTex&&!r.ao&&!r.aoRaw&&r.depthSnapshotFormat==D3DFMT_UNKNOWN);
        const auto color=r.scene;const auto colorSurface=r.sceneSurface;
        r.selectedBackend=1;r.worldDepthDescKnown=true;
        for(unsigned i=0;i<32;++i)assert(r.resources(3840,2160,10,true));
        assert(d.textures==1&&d.shaders==0);
        assert(r.resources(3840,2160,10));
        assert(r.scene==color&&r.sceneSurface==colorSurface&&d.textures==4&&d.shaders==5&&textureBytes==99532800);
        assert(r.depthSnapshotFormat==MAKEFOURCC('D','F','2','4'));
        assert(d.allocations[1].usage==D3DUSAGE_DEPTHSTENCIL&&d.allocations[1].w==3840&&d.allocations[1].h==2160);
        for(unsigned i=2;i<4;++i)assert(d.allocations[i].w==1920&&d.allocations[i].h==1080&&d.allocations[i].format==D3DFMT_A16B16G16R16F);
        const auto depthId=r.depthTex->id;
        r.worldDepthDesc.MultiSampleType=2;r.captured=r.rainDepthOk=r.rainMaskCleared=r.rainMaskDrawn=true;
        r.rainDepth=new Texture(0,3840,2160,10,&d.failSurface);r.rainMaskMS=new Surface;
        assert(r.resources(3840,2160,10,true)&&d.textures==4&&r.depthTex->id==depthId);
        assert(r.captured&&r.rainDepthOk&&r.rainMaskCleared&&r.rainMaskDrawn&&r.rainDepth&&r.rainMaskMS);
        assert(r.resources(3840,2160,10)&&d.textures==7&&r.depthTex->id!=depthId);
        assert(r.scene==color&&r.sceneSurface==colorSurface&&r.depthSnapshotFormat==MAKEFOURCC('I','N','T','Z'));
        assert(!r.captured&&!r.rainDepthOk&&!r.rainMaskCleared&&!r.rainMaskDrawn&&!r.rainDepth&&!r.rainMaskMS&&textureBytes==99532800);
        const auto retainedDepth=r.depthTex;
        assert(r.resources(3840,2160,20,true)&&d.textures==8&&r.depthTex==retainedDepth&&textureBytes==99532800);
        r.worldDepthDescKnown=false;
        assert(r.resources(3840,2160,20)&&d.textures==8&&r.depthTex==retainedDepth);
        assert(r.resources(800,600,20,true)&&d.textures==9&&!r.depthTex&&!r.ao&&!r.aoRaw&&textureBytes==1920000);
        assert(r.resources(800,600,20)&&d.textures==12&&d.shaders==5&&textureBytes==5760000);
    }
    assert(alive==0&&textureBytes==0);
    {
        Device d;Renderer r;r.ext=&d;r.selectedBackend=1;r.worldDepthDescKnown=true;d.failDF24=true;
        assert(r.resources(800,600,10,true)&&d.textures==1);
        assert(r.resources(800,600,10)&&d.textures==5&&r.df24Refused&&r.depthSnapshotFormat==MAKEFOURCC('I','N','T','Z'));
        assert(r.resources(800,600,10)&&d.textures==5);
    }
    assert(alive==0&&textureBytes==0);
    {
        Device d;Renderer r;r.ext=&d;d.failTextureAt=1;
        assert(!r.resources(800,600,10,true)&&!r.scene&&!r.sceneSurface&&d.shaders==0);
        d.failTextureAt=0;d.failSurface=true;r.failed=false;
        assert(!r.resources(800,600,10,true)&&r.scene&&!r.sceneSurface&&d.textures==2);
        const auto color=r.scene;d.failSurface=false;r.failed=false;
        assert(r.resources(800,600,10,true)&&r.scene==color&&d.textures==2);
        d.failShader=true;assert(!r.resources(800,600,10)&&r.scene==color&&d.textures==2);
        d.failShader=false;d.failTextureAt=3;r.failed=false;
        assert(!r.resources(800,600,10)&&r.scene==color&&!r.depthTex&&d.textures==3&&d.shaders==5);
        d.failTextureAt=5;r.failed=false;
        assert(!r.resources(800,600,10)&&r.scene==color&&r.depthTex&&!r.ao&&d.textures==5);
        d.failTextureAt=0;r.failed=false;
        assert(r.resources(800,600,10)&&r.scene==color&&d.textures==7&&d.shaders==5);
    }
    assert(alive==0&&textureBytes==0);
    Device device;Service service;World world;
    {
        Renderer r;r.ext=&device;r.settingsService=&service;r.world=&world;
        assert(r.resources(800,600,10));assert(device.textures==4&&device.shaders==5);
        const auto scene=r.scene;const auto depth=r.depthTex;const auto ao=r.ao;const auto raw=r.aoRaw;
        const DWORD* expected[]={kAoContactBloomShader,kHdaoContactBloomShader,kHbaoPlusContactBloomShader,kGtaoContactBloomShader};
        for(unsigned pass=0;pass<200;++pass){
            world.method=pass%4;world.enabled=bool(pass&4);r.nextFrame();assert(r.aoMethod==pass%4&&r.contactAO==bool(pass&4));
            assert(r.aoStrength==1.f&&r.aoRadius==2.f);
            assert(r.resources(800,600,10));assert(r.aoPS&&r.aoContactBloomPS&&r.aoContactBloomPS->code==expected[pass%4]);
            assert(r.scene==scene&&r.depthTex==depth&&r.ao==ao&&r.aoRaw==raw&&device.textures==4);
            auto count=device.shaders;r.nextFrame();assert(r.resources(800,600,10)&&device.shaders==count);
        }
        world.method=2;world.enabled=false;r.nextFrame();assert(r.aoMethod==2&&!r.contactAO);assert(r.resources(800,600,10));
        assert(r.aoContactBloomPS->code==kHbaoPlusContactBloomShader&&device.textures==4);
        const DWORD* qualityShaders[3][4]={
            {kAoContactBloomShader,kHdaoContactBloomShader,kHbaoPlusContactBloomShader,kGtaoContactBloomShader},
            {kAoContactBloomShader,kHdaoContactBloomMediumShader,kHbaoPlusContactBloomMediumShader,kGtaoContactBloomMediumShader},
            {kAoContactBloomShader,kHdaoContactBloomLowShader,kHbaoPlusContactBloomLowShader,kGtaoContactBloomLowShader}};
        for(unsigned method=0;method<4;++method)for(unsigned quality=0;quality<3;++quality){
            r.selectAO(method,quality);assert(r.resources(800,600,10));
            assert(r.aoContactBloomPS->code==qualityShaders[quality][method]);
            const auto shader=r.aoContactBloomPS;const auto count=device.shaders;
            r.selectAO(method,quality);assert(r.resources(800,600,10)&&r.aoContactBloomPS==shader&&device.shaders==count);
            assert(r.scene==scene&&r.depthTex==depth&&r.ao==ao&&r.aoRaw==raw&&device.textures==4);
        }
        r.selectAO(0,0);assert(r.resources(800,600,10));
        const auto ssao=r.aoContactBloomPS;const auto ssaoCount=device.shaders;
        r.selectAO(0,2);assert(r.resources(800,600,10)&&r.aoContactBloomPS==ssao&&device.shaders==ssaoCount);
        r.selectAO(2,0);assert(r.resources(800,600,10));
        world.method=3;world.enabled=true;r.nextFrame();device.failShader=true;assert(!r.resources(800,600,10)&&r.failed);
        device.failShader=false;r.failed=false;assert(r.resources(800,600,10)&&r.aoContactBloomPS->code==kGtaoContactBloomShader&&device.textures==4);
    }
    assert(alive==0);
    std::cout<<"AO live switch and lazy HDR: 200 frame transitions, exact allocation bytes, retained targets, depth-format changes, failure/retry and lifetimes passed\n";
}
'''.replace('__SELECT__', extract('    void selectAO(')).replace('__RESOURCES__', extract('    bool resources(UINT w, UINT h, D3DFORMAT format,')).replace('__FRAME__', ao_update)
with tempfile.TemporaryDirectory(prefix='northlight-ao-live-') as tmp:
    cpp = Path(tmp) / 'ao_live.cpp'
    exe = Path(tmp) / 'ao_live.exe'
    cpp.write_text(harness, encoding='utf-8')
    subprocess.run(['clang++', '-std=c++17', '-O2', '-UNDEBUG', *fp.test_include_flags(), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
