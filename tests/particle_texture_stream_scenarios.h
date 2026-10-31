#pragma once
// Uses the real stream/proxy/replayer with the existing fake backend. Cache
// inspections execute on the replay thread; no future producer state is read.
// The retained particle mip is at most 32 texels; atlas coverage below stays 128.
static void particleTextureStream(){
    using namespace NorthlightParticleTextures;
    enabled.store(true);
    // The cache keeps mips of at most Cache::MaxDimension (32) texels; larger uploads are not cached.
    Rig rig(true);IDirect3DTexture9* texture=nullptr;
    // Disabling direct dispatch also omits its production identity resolver.
    // This fake backend still proves raw identity, independently of dispatch.
    if(!kDirectReplay)CHECK(runTask(rig.core(),[](StreamCore& c){c.reg.rawOf=[](IUnknown* p,Kind){return unwrapFake(p);};}));
    CHECK(rig.dev->CreateTexture(32,16,0,0,(D3DFORMAT)21,(D3DPOOL)1,&texture,nullptr)==D3D_OK);
    rig.sync();auto* proxy=ProxyBase::of(texture);CHECK(proxy&&proxy->raw);
    const void* raw=proxy->raw;
    CHECK(runTask(rig.core(),[](StreamCore&){cache().clear();}));
    struct Inspect {const void* raw;bool found=false;float rgba[4]={};std::uint64_t life=0,revision=0;};
    auto inspect=[](void* value,StreamCore&){auto& result=*static_cast<Inspect*>(value);View v;result.found=cache().lookup(result.raw,v);if(result.found){CHECK(v.sample(.5f,.5f,false,3,3,result.rgba));result.life=v.lifetime;result.revision=v.revision;}};
    // Write descriptors are recorded while replay is blocked. The second
    // upload overwrites the SAME producer-side shadow before the first draw.
    gKnobs.hold.store(true);rig.dev->BeginScene();
    Inspect first{raw},second{raw};Task before{inspect,&first},after{inspect,&second};
    auto enqueue=[&](Task* task){auto& q=rig.core().q;auto** dst=static_cast<Task**>(q.reserve((std::uint16_t)Cmd::Quiesce,sizeof task));*dst=task;q.commit();};
    auto upload=[&](unsigned char blue,unsigned char red){D3DLOCKED_RECT locked{};CHECK(texture->LockRect(0,&locked,nullptr,0)==D3D_OK);
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<32;++x){auto* pixel=static_cast<unsigned char*>(locked.pBits)+y*locked.Pitch+x*4;pixel[0]=blue;pixel[1]=0;pixel[2]=red;pixel[3]=255;}
        CHECK(texture->UnlockRect(0)==D3D_OK);};
    upload(255,0);enqueue(&before);upload(0,255);enqueue(&after);gKnobs.hold.store(false);rig.sync();
    CHECK(first.found&&second.found&&first.rgba[2]==1&&first.rgba[0]==0&&second.rgba[0]==1&&second.rgba[2]==0);
    CHECK(first.life==second.life&&first.revision<second.revision);
    RECT partial{0,0,4,4};D3DLOCKED_RECT locked{};CHECK(texture->LockRect(0,&locked,&partial,0)==D3D_OK);CHECK(texture->UnlockRect(0)==D3D_OK);rig.sync();
    Inspect partialResult{raw};CHECK(runTask(rig.core(),[&](StreamCore& c){inspect(&partialResult,c);}));CHECK(!partialResult.found);
    upload(255,0);rig.sync();
    CHECK(rig.dev->UpdateTexture(texture,texture)==D3D_OK);rig.sync();
    Inspect gpu{raw};CHECK(runTask(rig.core(),[&](StreamCore& c){inspect(&gpu,c);}));CHECK(!gpu.found);
    texture->Release();rig.sync();
    CHECK(rig.dev->CreateTexture(32,16,0,0,(D3DFORMAT)21,(D3DPOOL)1,&texture,nullptr)==D3D_OK);upload(255,0);rig.sync();
    const void* rawAgain=ProxyBase::of(texture)->raw;CHECK(runTask(rig.core(),[&](StreamCore&){View v;CHECK(cache().lookup(rawAgain,v));}));
    texture->Release();rig.sync();
    Inspect released{rawAgain};CHECK(runTask(rig.core(),[&](StreamCore& c){inspect(&released,c);}));CHECK(!released.found);
    // A surface upload must be keyed by its parent 2D texture's raw identity,
    // with the actual derived mip dimensions, never by the surface pointer.
    CHECK(rig.dev->CreateTexture(64,64,0,0,(D3DFORMAT)21,(D3DPOOL)1,&texture,nullptr)==D3D_OK);
    IDirect3DSurface9* surface=nullptr;CHECK(texture->GetSurfaceLevel(1,&surface)==D3D_OK);
    CHECK(surface->LockRect(&locked,nullptr,0)==D3D_OK);for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){auto* pixel=static_cast<unsigned char*>(locked.pBits)+y*locked.Pitch+x*4;pixel[0]=0;pixel[1]=255;pixel[2]=0;pixel[3]=255;}CHECK(surface->UnlockRect()==D3D_OK);rig.sync();
    auto* surfaceProxy=ProxyBase::of(surface);CHECK(runTask(rig.core(),[&](StreamCore&){View v;CHECK(cache().lookup(ProxyBase::of(texture)->raw,v));CHECK(v.mip==1&&v.width==32&&v.height==32);CHECK(!cache().lookup(surfaceProxy->raw,v));}));
    CHECK(rig.dev->ColorFill(surface,nullptr,0)==D3D_OK);rig.sync();CHECK(runTask(rig.core(),[&](StreamCore&){View v;CHECK(!cache().lookup(ProxyBase::of(texture)->raw,v));}));
    surface->Release();texture->Release();rig.sync();rig.finish();checkClean();
    std::puts("particle texture stream: future producer shadow isolation, ordered uploads/revisions, partial and GPU writes, derived surface identity/mip, retirement PASS");
}

static void materialTextureStream(){
    Rig rig(true);IDirect3DTexture9* texture=nullptr;if(!kDirectReplay)CHECK(runTask(rig.core(),[](StreamCore& c){c.reg.rawOf=[](IUnknown* p,Kind){return unwrapFake(p);};}));
    CHECK(rig.dev->CreateTexture(256,256,1,0,(D3DFORMAT)21,(D3DPOOL)1,&texture,nullptr)==D3D_OK);rig.sync();const void* raw=ProxyBase::of(texture)->raw;CHECK(raw);
    struct Inspect{const void* raw;bool found=false;float red=0,blue=0;std::uint64_t revision=0;};
    auto inspect=[](void*p,StreamCore&){auto& out=*static_cast<Inspect*>(p);NorthlightParticleTextures::View view;out.found=NorthlightMaterialTextures::cache().lookup(out.raw,view);if(out.found){CHECK(view.width==128&&view.height==128);float rgba[4];CHECK(view.sample(.5f,.5f,false,3,3,rgba));out.red=rgba[0];out.blue=rgba[2];out.revision=view.revision;}};
    auto enqueue=[&](Task* t){auto& q=rig.core().q;auto** slot=static_cast<Task**>(q.reserve((std::uint16_t)Cmd::Quiesce,sizeof t));*slot=t;q.commit();};
    auto upload=[&](unsigned char blue,unsigned char red){D3DLOCKED_RECT lock{};CHECK(texture->LockRect(0,&lock,nullptr,0)==D3D_OK);for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){auto*p=static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch+x*4;p[0]=blue;p[1]=0;p[2]=red;p[3]=255;}CHECK(texture->UnlockRect(0)==D3D_OK);};

    Inspect first{raw},second{raw};Task a{inspect,&first},b{inspect,&second};gKnobs.hold.store(true);rig.dev->BeginScene();upload(255,0);enqueue(&a);upload(0,255);enqueue(&b);gKnobs.hold.store(false);rig.sync();CHECK(first.found&&second.found&&first.blue==1&&second.red==1&&first.revision<second.revision);
    RECT rect{64,64,192,192};D3DLOCKED_RECT lock{};CHECK(texture->LockRect(0,&lock,&rect,0)==D3D_OK);for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x){auto*p=static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch+x*4;p[0]=255;p[1]=p[2]=0;p[3]=255;}CHECK(texture->UnlockRect(0)==D3D_OK);rig.sync();Inspect partial{raw};CHECK(runTask(rig.core(),[&](StreamCore& c){inspect(&partial,c);}));CHECK(partial.found&&partial.blue==1&&partial.revision>second.revision);
    texture->Release();rig.sync();Inspect released{raw};CHECK(runTask(rig.core(),[&](StreamCore& c){inspect(&released,c);}));CHECK(!released.found);rig.finish();checkClean();std::puts("material texture stream: ordered atlas uploads, partial equipment update, no future producer pixels, retirement PASS");
}
