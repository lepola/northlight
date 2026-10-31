#pragma once
#include "material_disk_cache.h"
#include "actor_texture.h"
#include "material_height.h"

// D3D stays on its owner. Static source snapshots and completed lockable copies
// cross to the disk worker only as owned CPU bytes, never as COM objects.
class NorthlightMaterialMapPersistence {
    friend struct MaterialDiskGpuTestAccess;
    using Store=NorthlightMaterialDisk::Store;
    using Bytes=NorthlightMaterialDisk::Bytes;
    using Key=NorthlightMaterialDisk::Key;
    IDirect3DDevice9* d;
    Store store;
    Bytes environment;
    struct Read {
        IDirect3DTexture9* source=nullptr;std::uint64_t revision=0,used=0;bool model=false;unsigned polarity=0,details=0,balance=0;
        std::array<float,4> profile{};Store::Handle ticket;
        ~Read(){drop(source);}
    };
    struct Save {
        IDirect3DSurface9* surface=nullptr;IDirect3DQuery9* event=nullptr;Key key;unsigned width=0,height=0;std::uint64_t frame=0;Bytes pixels;
        ~Save(){drop(surface);drop(event);}
    };
    std::vector<std::unique_ptr<Read>> reads;
    std::vector<std::unique_ptr<Save>> saves;
    std::uint64_t frame=0;bool captured=false,queuedReadback=false;
    template<class T>static void append(Bytes& bytes,const T& value){const auto* p=reinterpret_cast<const std::uint8_t*>(&value);bytes.insert(bytes.end(),p,p+sizeof value);}
    static bool layout(D3DFORMAT format,unsigned width,unsigned height,size_t& row,size_t& rows){
        using F=NorthlightActorTexture::Format;F f;
        switch(format){case D3DFMT_A8R8G8B8:f=F::BGRA8;break;case D3DFMT_X8R8G8B8:f=F::BGRX8;break;case D3DFMT_R5G6B5:f=F::RGB565;break;
        case D3DFMT_X1R5G5B5:f=F::XRGB1555;break;case D3DFMT_A1R5G5B5:f=F::ARGB1555;break;case D3DFMT_A4R4G4B4:f=F::ARGB4444;break;
        case D3DFMT_DXT1:f=F::BC1;break;case D3DFMT_DXT3:f=F::BC2;break;case D3DFMT_DXT5:f=F::BC3;break;default:return false;}
        row=NorthlightActorTexture::rowBytes(width,f);rows=NorthlightActorTexture::rowCount(height,f);return true;
    }
    void forget(const Store::Handle& ticket){reads.erase(std::remove_if(reads.begin(),reads.end(),[&](const auto& p){return p->ticket==ticket;}),reads.end());}
public:
    enum class Status {Skip,Pending,Miss,Hit};
    struct Probe {Status status=Status::Skip;std::array<float,4> profile{};Store::Handle ticket;};
    std::uint64_t uploads=0,copies=0,busy=0,unsupported=0;
    NorthlightMaterialMapPersistence(IDirect3DDevice9* device,const std::filesystem::path& path,const char* backend,const char* backendIdentity=nullptr,bool asyncReadback=false):d(device),store(path),queuedReadback(asyncReadback){
        const unsigned version=2;append(environment,version);
        const auto shaders=NorthlightMaterialDisk::hash(kAutoNormalShader,sizeof kAutoNormalShader,NorthlightMaterialDisk::hash(kAutoNormalModelShader,sizeof kAutoNormalModelShader));append(environment,shaders);
        if(backend)environment.insert(environment.end(),backend,backend+std::strlen(backend));
        if(backendIdentity)environment.insert(environment.end(),backendIdentity,backendIdentity+std::strlen(backendIdentity));
        D3DDEVICE_CREATION_PARAMETERS creation{};IDirect3D9* api=nullptr;D3DADAPTER_IDENTIFIER9 adapter{};
        if(SUCCEEDED(d->GetCreationParameters(&creation))&&SUCCEEDED(d->GetDirect3D(&api))&&api){
            if(SUCCEEDED(api->GetAdapterIdentifier(creation.AdapterOrdinal,0,&adapter))){append(environment,adapter.VendorId);append(environment,adapter.DeviceId);append(environment,adapter.DriverVersion);append(environment,adapter.DeviceIdentifier);}drop(api);
        }
    }
    ~NorthlightMaterialMapPersistence(){reset();}
    void reset(){reads.clear();saves.clear();captured=false;}
    Probe probe(IDirect3DTexture9* source,const D3DSURFACE_DESC& desc,bool model,unsigned polarity,unsigned details,unsigned balance){
        if(!store.available()||(desc.Usage&(D3DUSAGE_DYNAMIC|D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL))||desc.Pool!=D3DPOOL_MANAGED)return {};
        if(model){polarity=polarity==2?2u:0u;details=0;balance=50;} // Only the model height sign affects its generator.
        const auto revision=NorthlightMaterialHeight::version(source);
        reads.erase(std::remove_if(reads.begin(),reads.end(),[&](const auto& r){return r->source==source&&r->revision!=revision;}),reads.end());
        for(auto& r:reads)if(r->source==source&&r->model==model&&r->polarity==polarity&&r->details==details&&r->balance==balance){
            r->used=frame;const auto state=r->ticket->state.load(std::memory_order_acquire);
            const auto status=state==Store::State::Hit?Status::Hit:state==Store::State::Miss?Status::Miss:state==Store::State::Pending?Status::Pending:Status::Skip;
            return {status,r->profile,r->ticket};
        }
        size_t row=0,rows=0;if(!desc.Width||!desc.Height||desc.Width>16384||desc.Height>16384||!layout(desc.Format,desc.Width,desc.Height,row,rows)||!row||!rows||row>Store::MaxSourceBytes||rows>Store::MaxSourceBytes/row){++unsupported;return {};}
        if(captured||reads.size()>=Store::MaxJobs)return {Status::Pending,{},{}};
        captured=true;
        IDirect3DSurface9* surface=nullptr;if(FAILED(source->GetSurfaceLevel(0,&surface))||!surface)return {};
        D3DLOCKED_RECT lock{};const HRESULT hr=surface->LockRect(&lock,nullptr,D3DLOCK_READONLY|D3DLOCK_DONOTWAIT);
        if(FAILED(hr)){drop(surface);if(hr==D3DERR_WASSTILLDRAWING){++busy;return {Status::Pending,{},{}};}++unsupported;return {};}
        Bytes bytes;bool valid=lock.pBits&&lock.Pitch>0&&size_t(lock.Pitch)>=row&&rows-1<=(SIZE_MAX-row)/size_t(lock.Pitch);
        try{if(valid){bytes.resize(row*rows);for(size_t y=0;y<rows;++y)std::memcpy(bytes.data()+y*row,static_cast<const std::uint8_t*>(lock.pBits)+y*size_t(lock.Pitch),row);}}
        catch(...){surface->UnlockRect();drop(surface);return {};}
        const auto unlocked=surface->UnlockRect();drop(surface);if(!valid||FAILED(unlocked))return {};
        auto record=std::make_unique<Read>();record->source=source;source->AddRef();record->revision=revision;record->used=frame;record->model=model;record->polarity=polarity;record->details=details;record->balance=balance;
        NorthlightMaterialHeight::Image image;if(!model)NorthlightMaterialHeight::read(source,image);record->profile=NorthlightMaterialHeight::profile(image,polarity,details,balance);
        Bytes identity=environment;append(identity,desc.Width);append(identity,desc.Height);append(identity,desc.Format);append(identity,model);append(identity,record->profile);
        record->ticket=store.request(std::move(identity),std::move(bytes),std::min(desc.Width,512u),std::min(desc.Height,512u));
        if(!record->ticket)return {Status::Pending,{},{}};
        reads.push_back(std::move(record));return {Status::Pending,{},{}};
    }
    bool upload(const Probe& probe,IDirect3DTexture9* normal,unsigned width,unsigned height){
        if(probe.status!=Status::Hit||!probe.ticket||probe.ticket->pixels.size()!=size_t(width)*height*4)return false;
        IDirect3DSurface9 *staging=nullptr,*target=nullptr;D3DLOCKED_RECT lock{};
        if(FAILED(d->CreateOffscreenPlainSurface(width,height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr)))return false;
        bool ok=SUCCEEDED(staging->LockRect(&lock,nullptr,D3DLOCK_DONOTWAIT));
        if(ok){const bool valid=lock.pBits&&lock.Pitch>=INT(width*4)&&height-1<=(SIZE_MAX-width*4)/size_t(lock.Pitch);if(valid)for(unsigned y=0;y<height;++y)std::memcpy(static_cast<std::uint8_t*>(lock.pBits)+size_t(y)*lock.Pitch,probe.ticket->pixels.data()+size_t(y)*width*4,width*4);ok=SUCCEEDED(staging->UnlockRect())&&valid;}
        if(ok)ok=SUCCEEDED(normal->GetSurfaceLevel(0,&target))&&SUCCEEDED(d->UpdateSurface(staging,nullptr,target,nullptr));
        drop(staging);drop(target);if(ok){++uploads;forget(probe.ticket);}return ok;
    }
    void capture(const Probe& probe,IDirect3DTexture9* normal,unsigned width,unsigned height){
        if(probe.status!=Status::Miss||!probe.ticket)return;
        if(saves.size()<4){
            auto save=std::make_unique<Save>();save->key=probe.ticket->key;save->width=width;save->height=height;save->frame=frame;
            IDirect3DSurface9* source=nullptr;
            // Packaged DXVK 2/3 enqueue GetRenderTargetData without waiting.
            // Native D3D9 uses a lockable RT instead; its GetRTData may block.
            bool ok=SUCCEEDED(queuedReadback?d->CreateOffscreenPlainSurface(width,height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&save->surface,nullptr):d->CreateRenderTarget(width,height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,TRUE,&save->surface,nullptr))&&
                SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_EVENT,&save->event))&&SUCCEEDED(normal->GetSurfaceLevel(0,&source));
            if(ok)ok=SUCCEEDED(queuedReadback?d->GetRenderTargetData(source,save->surface):d->StretchRect(source,nullptr,save->surface,nullptr,D3DTEXF_NONE))&&SUCCEEDED(save->event->Issue(D3DISSUE_END));
            drop(source);if(ok){++copies;saves.push_back(std::move(save));}
        }
        forget(probe.ticket);
    }
    void endFrame(){
        ++frame;captured=false;
        reads.erase(std::remove_if(reads.begin(),reads.end(),[&](const auto& r){return frame-r->used>2;}),reads.end());
        // At most one 1 MiB CPU copy per frame. No FLUSH, sleep, polling loop,
        // uncompleted readback or file operation can enter the owner's path.
        if(saves.empty())return;
        auto& save=*saves.front();if(frame-save.frame>600){saves.erase(saves.begin());return;}
        if(!save.pixels.empty()){if(store.save(save.key,save.width,save.height,save.pixels))saves.erase(saves.begin());return;}
        const auto ready=save.event->GetData(nullptr,0,0);if(ready==S_FALSE){++busy;return;}if(FAILED(ready)){saves.erase(saves.begin());return;}
        D3DLOCKED_RECT lock{};const auto hr=save.surface->LockRect(&lock,nullptr,D3DLOCK_READONLY|D3DLOCK_DONOTWAIT);
        if(hr==D3DERR_WASSTILLDRAWING){++busy;return;}if(FAILED(hr)){saves.erase(saves.begin());return;}
        Bytes pixels;bool valid=lock.pBits&&lock.Pitch>=INT(save.width*4)&&save.height-1<=(SIZE_MAX-save.width*4)/size_t(lock.Pitch);
        try{if(valid){pixels.resize(size_t(save.width)*save.height*4);for(unsigned y=0;y<save.height;++y)std::memcpy(pixels.data()+size_t(y)*save.width*4,static_cast<const std::uint8_t*>(lock.pBits)+size_t(y)*lock.Pitch,save.width*4);}}
        catch(...){save.surface->UnlockRect();saves.erase(saves.begin());return;}
        valid=SUCCEEDED(save.surface->UnlockRect())&&valid;
        // A generated tangent normal cannot have both encoded X and Y zero.
        // Refuse broken readback implementations rather than persist blank maps.
        if(valid)for(size_t i=0;i<pixels.size();i+=4)if(!pixels[i+1]&&!pixels[i+2]){valid=false;++unsupported;break;}
        if(valid){save.pixels=std::move(pixels);drop(save.surface);drop(save.event);if(store.save(save.key,save.width,save.height,save.pixels))saves.erase(saves.begin());}
        else saves.erase(saves.begin());
    }
    void report(){logf("MATERIAL_DISK hits=%llu misses=%llu writes=%llu invalid=%llu errors=%llu refused=%llu evicted=%llu uploads=%llu copies=%llu busy=%llu unsupported=%llu pendingReads=%zu pendingCopies=%zu cpuBytes=%zu",(unsigned long long)store.stats.hits.load(),(unsigned long long)store.stats.misses.load(),(unsigned long long)store.stats.writes.load(),(unsigned long long)store.stats.invalid.load(),(unsigned long long)store.stats.errors.load(),(unsigned long long)store.stats.refused.load(),(unsigned long long)store.stats.evicted.load(),(unsigned long long)uploads,(unsigned long long)copies,(unsigned long long)busy,(unsigned long long)unsupported,reads.size(),saves.size(),store.outstandingBytes());}
};
