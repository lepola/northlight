#pragma once

// The owner borrows the REAL device and must clear this pool before Reset and
// before releasing that device. D3DSBT_ALL blocks retain captured resource refs.
// Render-thread only (0.3.192 CS: the replay thread when the command stream runs); owner lifetime must enclose every SavedState scope.
class NorthlightStateBlockPool {
    struct Slot { IDirect3DStateBlock9* block=nullptr; bool busy=false; };
    IDirect3DDevice9* device;
    Slot slots[4];
public:
    explicit NorthlightStateBlockPool(IDirect3DDevice9* realDevice):device(realDevice){}
    NorthlightStateBlockPool(const NorthlightStateBlockPool&)=delete;
    NorthlightStateBlockPool& operator=(const NorthlightStateBlockPool&)=delete;
    ~NorthlightStateBlockPool(){clear();}
    bool ownsDevice(IDirect3DDevice9* other)const{return device==other;}
    IDirect3DStateBlock9* acquire(){
        if(!device)return nullptr;
        for(auto& slot:slots)if(!slot.busy){
            if(slot.block){
                if(FAILED(slot.block->Capture())){
                    slot.block->Release();slot.block=nullptr;return nullptr;
                }
            }else{
                // CreateStateBlock(D3DSBT_ALL) captures the initial state itself.
                if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&slot.block))){
                    if(slot.block)slot.block->Release();slot.block=nullptr;return nullptr;
                }
            }
            slot.busy=slot.block!=nullptr;return slot.block;
        }
        // Deep nesting is correct without growing the retained pool.
        IDirect3DStateBlock9* overflow=nullptr;
        if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&overflow))){
            if(overflow)overflow->Release();return nullptr;
        }
        return overflow;
    }
    void release(IDirect3DStateBlock9* block){
        if(!block)return;
        for(auto& slot:slots)if(slot.block==block){slot.busy=false;return;}
        block->Release(); // Overflow or a lease detached by clear().
    }
    void clear(){
        for(auto& slot:slots){
            // An active lease owns the block until restoration completes. This
            // permits resource reset/resize within a SavedState scope safely.
            if(slot.block&&!slot.busy)slot.block->Release();
            slot.block=nullptr;slot.busy=false;
        }
    }
};

// ALL does not capture render targets/depth-stencil; preserve those explicitly.
struct SavedState {
    IDirect3DDevice9* d;
    NorthlightStateBlockPool* pool;
    IDirect3DStateBlock9* block=nullptr;
    IDirect3DSurface9* targets[4]={};
    IDirect3DSurface9* depth=nullptr;
    D3DVIEWPORT9 viewport={};
    bool ok=false;
    explicit SavedState(IDirect3DDevice9* device,NorthlightStateBlockPool* cache=nullptr)
        :d(device),pool(cache&&cache->ownsDevice(device)?cache:nullptr){
        if(!d)return;
        if(pool)block=pool->acquire();
        else if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&block)))return;
        if(!block)return;
        for(int i=0;i<4;++i)d->GetRenderTarget(i,&targets[i]);
        d->GetDepthStencilSurface(&depth);
        ok=targets[0]&&SUCCEEDED(d->GetViewport(&viewport));
    }
    SavedState(const SavedState&)=delete;
    SavedState& operator=(const SavedState&)=delete;
    ~SavedState(){
        if(ok){
            d->SetDepthStencilSurface(nullptr);
            for(int i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
            d->SetRenderTarget(0,targets[0]);
            for(int i=1;i<4;++i)d->SetRenderTarget(i,targets[i]);
            d->SetDepthStencilSurface(depth);
            block->Apply();d->SetViewport(&viewport);
        }
        if(pool)pool->release(block);else if(block)block->Release();
        if(depth)depth->Release();
        for(auto* target:targets)if(target)target->Release();
    }
};
