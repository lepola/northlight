#pragma once
// Fault injection is local to the standalone device wrapper; native calls are
// forwarded until a case explicitly enables fake EVENT queries.
struct UploadFaultDevice:ForwardIDirect3DDevice9 {
    struct Query;
    unsigned indices=0,failAt=0,textures=0,failTextureAt=0;
    bool fakeQueries=false,queryFlushed=false;
    std::function<bool()> recoveryAdmission;unsigned recoveryAdmissions=0;uint64_t lowestRecoveryAdmission=UINT64_MAX;
    bool auditAllocations=false;unsigned badAdmissions=0,auditedAllocations=0;uint64_t lowestAdmissionAvailable=UINT64_MAX;
    bool traceAllocations=std::getenv("NORTHLIGHT_TRACE_ALLOCATIONS")!=nullptr;uint64_t lastAllocationAvailable=0;unsigned allocationSequence=0;std::set<uintptr_t> allocationBases;
    static uint64_t availableVA(){MEMORYSTATUSEX m{};m.dwLength=sizeof m;return GlobalMemoryStatusEx(&m)?m.ullAvailVirtual:0;}
    void allocationRegions(bool report){MEMORY_BASIC_INFORMATION m{};uintptr_t p=0;while(VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof m)){
        const auto base=reinterpret_cast<uintptr_t>(m.AllocationBase);if(m.State!=MEM_FREE&&m.RegionSize>=(8u<<20)&&allocationBases.insert(base).second&&report)
            std::printf("ALLOCATION_NEW_REGION base=%p region=%p bytes=%llu state=%lu type=%lu protect=%lu allocationProtect=%lu\n",m.AllocationBase,m.BaseAddress,(unsigned long long)m.RegionSize,(unsigned long)m.State,(unsigned long)m.Type,(unsigned long)m.Protect,(unsigned long)m.AllocationProtect);
        const uintptr_t end=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;if(end<=p)break;p=end;}}
    uint64_t allocationBegin(const char* kind,UINT width,UINT height,DWORD usage,DWORD format,D3DPOOL pool){if(!traceAllocations)return 0;
        if(!allocationSequence)allocationRegions(false);const auto before=availableVA();++allocationSequence;
        if(lastAllocationAvailable>before+(8u<<20)){std::printf("ALLOCATION_BETWEEN_CALLS next=%s sequence=%u beforeMiB=%.6f priorAfterMiB=%.6f\n",kind,allocationSequence,double(before)/1048576,double(lastAllocationAvailable)/1048576);allocationRegions(true);}
        (void)width;(void)height;(void)usage;(void)format;(void)pool;return before;}
    HRESULT allocationEnd(HRESULT hr,uint64_t before,const char* kind,UINT width,UINT height,DWORD usage,DWORD format,D3DPOOL pool){if(traceAllocations){const auto after=availableVA();
        if(before>after+(8u<<20)){std::printf("ALLOCATION_CALL_JUMP kind=%s sequence=%u widthOrBytes=%u height=%u usage=%lu format=%lu pool=%u beforeMiB=%.6f afterMiB=%.6f hr=%08lx\n",kind,allocationSequence,width,height,(unsigned long)usage,(unsigned long)format,unsigned(pool),double(before)/1048576,double(after)/1048576,(unsigned long)hr);allocationRegions(true);}lastAllocationAvailable=after;}return hr;}

    void audit(NorthlightGeometryMemory::Budget budget,bool allowRecovery=false){
        if(!auditAllocations)return;++auditedAllocations;const bool recovery=allowRecovery&&recoveryAdmission&&recoveryAdmission();budget=NorthlightGeometryMemory::replayBudget(budget,recovery);MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;
        const bool sampled=GlobalMemoryStatusEx(&memory)!=FALSE;const uint64_t available=sampled?NorthlightGeometryMemory::corrected(memory.ullAvailVirtual):0;
        lowestAdmissionAvailable=std::min(lowestAdmissionAvailable,available);if(recovery){++recoveryAdmissions;lowestRecoveryAdmission=std::min(lowestRecoveryAdmission,available);}
        if(!sampled||!budget.valid||available<budget.available){++badAdmissions;std::printf("ALLOCATION_ADMISSION_FAILED availableMiB=%.6f requiredMiB=%.6f\n",double(available)/1048576,double(budget.available)/1048576);}
    }
    static uint64_t textureBytes(UINT width,UINT height,UINT levels,D3DFORMAT format){
        unsigned pixel=0,block=0;
        switch(format){case D3DFMT_A32B32G32R32F:pixel=16;break;case D3DFMT_A16B16G16R16F:case D3DFMT_G32R32F:pixel=8;break;
        case D3DFMT_A8R8G8B8:case D3DFMT_X8R8G8B8:case D3DFMT_R32F:case D3DFMT_G16R16F:pixel=4;break;
        case D3DFMT_R16F:case D3DFMT_A8L8:pixel=2;break;case D3DFMT_L8:case D3DFMT_A8:pixel=1;break;
        case D3DFMT_DXT1:block=8;break;case D3DFMT_DXT3:case D3DFMT_DXT5:block=16;break;default:return 0;}
        uint64_t bytes=0;for(unsigned level=0;;++level){bytes+=block?uint64_t((width+3)/4)*((height+3)/4)*block:uint64_t(width)*height*pixel;
            if((levels&&level+1>=levels)||(!levels&&width==1&&height==1))break;width=std::max(1u,width/2);height=std::max(1u,height/2);}
        return bytes;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT bytes,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9** out,HANDLE* handle)override{
        audit(NorthlightGeometryMemory::dynamicBudget(bytes),pool==D3DPOOL_DEFAULT);const auto before=allocationBegin("VB",bytes,1,usage,fvf,pool);return allocationEnd(real->CreateVertexBuffer(bytes,usage,fvf,pool,out,handle),before,"VB",bytes,1,usage,fvf,pool);
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT width,UINT height,D3DFORMAT format,D3DMULTISAMPLE_TYPE samples,DWORD quality,BOOL discard,IDirect3DSurface9** out,HANDLE* handle)override{
        audit(NorthlightGeometryMemory::defaultTargetBudget(),true);const auto before=allocationBegin("Depth",width,height,samples,format,D3DPOOL_DEFAULT);return allocationEnd(real->CreateDepthStencilSurface(width,height,format,samples,quality,discard,out,handle),before,"Depth",width,height,samples,format,D3DPOOL_DEFAULT);
    }
    HRESULT queryCreateResult=S_OK,queryIssueResult=S_OK,queryDataResult=S_FALSE;
    unsigned queryCreates=0,queryIssues=0,queryReads=0,queryAlive=0,queryReferences=0;
    explicit UploadFaultDevice(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT bytes,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DIndexBuffer9** out,HANDLE* handle)override{
        if(++indices==failAt){if(out)*out=nullptr;return E_OUTOFMEMORY;}audit(NorthlightGeometryMemory::dynamicBudget(bytes),pool==D3DPOOL_DEFAULT);const auto before=allocationBegin("IB",bytes,1,usage,format,pool);return allocationEnd(real->CreateIndexBuffer(bytes,usage,format,pool,out,handle),before,"IB",bytes,1,usage,format,pool);
    }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT width,UINT height,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* handle)override{
        if(++textures==failTextureAt){if(out)*out=nullptr;return E_OUTOFMEMORY;}if(pool==D3DPOOL_MANAGED){const auto bytes=textureBytes(width,height,levels,format);audit(bytes?NorthlightGeometryMemory::dynamicBudget(bytes):NorthlightGeometryMemory::Budget{},format==D3DFMT_A8R8G8B8);}
        else if(pool==D3DPOOL_DEFAULT&&(usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL)))audit(NorthlightGeometryMemory::defaultTargetBudget(),true);
        const auto before=allocationBegin("Texture",width,height,usage,format,pool);return allocationEnd(real->CreateTexture(width,height,levels,usage,format,pool,out,handle),before,"Texture",width,height,usage,format,pool);
    }
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE type,IDirect3DQuery9** out)override;
};
struct UploadFaultDevice::Query:IDirect3DQuery9 {
    UploadFaultDevice& owner;ULONG refs=1;
    explicit Query(UploadFaultDevice& d):owner(d){++owner.queryAlive;++owner.queryReferences;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=IID_IUnknown&&id!=IID_IDirect3DQuery9)return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{++owner.queryReferences;return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{
        assert(refs&&owner.queryReferences);--owner.queryReferences;const auto left=--refs;
        if(!left){--owner.queryAlive;delete this;}return left;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out)override{if(!out)return D3DERR_INVALIDCALL;*out=owner.real;owner.real->AddRef();return S_OK;}
    D3DQUERYTYPE STDMETHODCALLTYPE GetType()override{return D3DQUERYTYPE_EVENT;}
    DWORD STDMETHODCALLTYPE GetDataSize()override{return sizeof(BOOL);}
    HRESULT STDMETHODCALLTYPE Issue(DWORD flags)override{assert(flags==D3DISSUE_END);++owner.queryIssues;return owner.queryIssueResult;}
    HRESULT STDMETHODCALLTYPE GetData(void*,DWORD,DWORD flags)override{++owner.queryReads;owner.queryFlushed|=(flags&D3DGETDATA_FLUSH)!=0;return owner.queryDataResult;}
};
inline HRESULT UploadFaultDevice::CreateQuery(D3DQUERYTYPE type,IDirect3DQuery9** out){
    if(!fakeQueries||type!=D3DQUERYTYPE_EVENT)return real->CreateQuery(type,out);
    ++queryCreates;if(out)*out=nullptr;if(FAILED(queryCreateResult))return queryCreateResult;
    if(out)*out=new Query(*this);return S_OK;
}
