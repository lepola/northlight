// Immutable source bounds, reevaluated with this draw's current pose and camera.
// Cheap and heavy evaluation have independent budgets. Cold construction never
// evaluates a pose and cannot spend either evaluation budget.
class EnvelopeCache {
public:
    class Prepared {
        friend class EnvelopeCache;
        NorthlightActorDeformation::Program program_;
        std::vector<D3DVERTEXELEMENT9> declaration_;
        std::vector<std::uint32_t> key_;
        std::uint64_t hash_=0;
        Prepared()=default;
    public:
        Prepared(const Prepared&)=delete;Prepared& operator=(const Prepared&)=delete;
        size_t bytes()const{return sizeof(Prepared)+declaration_.capacity()*sizeof(D3DVERTEXELEMENT9)+key_.capacity()*4+
            program_.operations.capacity()*sizeof(NorthlightActorDeformation::Operation)+program_.inputs.capacity()*sizeof(NorthlightActorDeformation::Input)+program_.definitions.capacity()*sizeof(NorthlightActorDeformation::Definition);}
    };
    enum class Stage {All,Ready,Build,Cheap,Heavy};
    enum class Kind {Unknown,Cold,Cheap,Heavy,Unsupported};
    struct WorkInfo {
        Kind kind=Kind::Unknown;std::uint64_t programHash=0;size_t groups=0,operations=0;double elapsedUs=0;bool skin=false;
    };
    struct Expensive {
        bool valid=false;std::uint64_t frame=0,programHash=0;size_t groups=0,operations=0;double elapsedUs=0;
        Stage stage=Stage::Heavy;Status status=Status::Budget;bool skin=false;
    };
    struct Stats {
        size_t ready=0,skinValid=0,cold=0,buildVertices=0,buildIndices=0,completed=0,buildSkippedReady=0;
        size_t timeDeferred=0,vertexDeferred=0,operationDeferred=0,unsupported=0,tupleLimit=0,invalid=0;
        size_t evictions=0,readyEvictions=0,expiredEvictions=0,releasedGroupBytes=0;
        size_t cheapValid=0,heavyCandidates=0,heavyAttempts=0,heavyValid=0,cheapSliceDeferred=0;
        size_t pendingResumes=0,pendingDeferred=0,pendingOwnerExpired=0,pendingAbandoned=0;
        size_t memoTests=0,memoHits=0,memoMisses=0,memoStored=0,memoEvictions=0,memoRejected=0,memoComparedBytes=0,memoAvoidedOperations=0,memoBudgetDeferred=0;
        size_t memoColdMisses=0,memoReplacedMisses=0,memoCameraMisses=0,memoConstantMisses=0;
        size_t memoBypassed=0,memoStoreBypassed=0,memoProtected=0,memoCopiedBytes=0;
        size_t effectiveCopiedBytes=0,fastValid=0;
        size_t sourceIdentityHits=0,sourceReconstructedHits=0;
    };
    struct MemoTotals {
        std::uint64_t tests=0,hits=0,cold=0,replaced=0,camera=0,constants=0;
        std::uint64_t bypassed=0,storeBypassed=0,protectedStores=0,copiedBytes=0;
        void add(const Stats& s){
            tests+=s.memoTests;hits+=s.memoHits;cold+=s.memoColdMisses;replaced+=s.memoReplacedMisses;
            camera+=s.memoCameraMisses;constants+=s.memoConstantMisses;bypassed+=s.memoBypassed;
            storeBypassed+=s.memoStoreBypassed;protectedStores+=s.memoProtected;copiedBytes+=s.memoCopiedBytes;
        }
    };
private:
    struct Binding {unsigned reg;D3DVERTEXELEMENT9 element;};
    struct Group {std::vector<float> limits;}; // low/high for USED input registers only
    struct Entry {
        std::weak_ptr<const NorthlightDrawSnapshot::Mesh> owner;
        const NorthlightDrawSnapshot::Mesh* sourceAddress=nullptr;
        NorthlightSourceIdentity::Handle identity;
        std::vector<std::uint32_t> key;
        std::vector<Binding> bindings;
        std::unordered_map<std::string,Group> groups;
        std::unique_ptr<SkinEnvelope> skin;
        size_t vertex=0,index=0,indexCount=0;bool unsupported=false,ready=false;
        std::uint64_t touched=0,programHash=0,lastEvaluationNs=0,serial=0;size_t bytes=0,baseBytes=0;
        unsigned constantFirst=0,constantCount=256,memoSlot=64;
        bool copyDefinitions=true,singleBone=false; /* singleBone resolves its control DEF directly */
    };
    static constexpr size_t MaxGroups=512,Limit=8u*1024u*1024u,MaxEntries=2048;
    static constexpr size_t MemoSlots=64,MemoLimit=256u*1024u;
    struct Memo {std::uint64_t serial=0,lastUsedFrame=0;std::vector<float> key;Bounds bounds;size_t bytes=0;};
    std::vector<Memo> memo_;
    NorthlightReplayMemo::Policy memoPolicy_;
    MemoTotals memoTotals_;
    bool memoProtect_=true;
    size_t memoBytes_=0,memoTableBytes_=0,memoNext_=0,memoEntries_=0;
    std::uint64_t entrySerial_=0;
    static constexpr std::uint64_t HeavyNanoseconds=AdaptiveTimeBudget::Heavy,CheapSliceNanoseconds=40000;
    AdaptiveTimeBudget timeBudget_;bool diagnostics_=true;
    size_t bytes_=0;
    std::unordered_multimap<std::uint64_t,Entry> entries_;
    std::uint64_t clock_=0,frame_=0,readyNanoseconds_=0,heavyNanoseconds_=0,buildNanoseconds_=0,classifyNanoseconds_=0;
    unsigned readyCalls_=0,heavyCalls_=0,buildCalls_=0;Stats stats_;Expensive expensive_;
    std::vector<std::uint32_t> scratch_;
    // One admitted immutable source build owns the cold turn until completion.
    // Map rehash preserves Entry addresses; erase/clear always release this
    // non-owning pointer first. No geometry owner or evaluated pose is retained.
    Entry* pending_=nullptr;
    std::uint64_t pendingProgressFrame_=0;
    /* FastReady clock batch: the interval since fastMark_ (warm cheap draws and
       the schedule between them) is charged to the ready bank every
       ReadyClockStride draws and before any Timer/lending starts. Gaps are
       charged, never dropped, so the batch only over-counts, by construction. */
    BoundsClock::time_point fastMark_;bool fastOpen_=false;unsigned fastCount_=0;
    static std::uint64_t elapsedNs(BoundsClock::time_point a,BoundsClock::time_point b){return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(a-b).count());}
    void flushFast(BoundsClock::time_point now){if(fastOpen_){readyNanoseconds_+=elapsedNs(now,fastMark_);fastOpen_=false;}}
    /* Owner equivalence without lock(): same control block and a live argument
       proves the same unexpired snapshot; an ABA address has another block. */
    static bool sameOwner(const std::weak_ptr<const NorthlightDrawSnapshot::Mesh>& a,const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& b){return !a.owner_before(b)&&!b.owner_before(a);}
    static const auto& identity(const NorthlightDrawSnapshot::Mesh& mesh){return mesh.replayIdentity?mesh.replayIdentity:mesh.sourceIdentity;}
    static std::uint64_t sourceHash(const NorthlightDrawSnapshot::Mesh& mesh){const auto& id=identity(mesh);return id?id->hash:std::uint64_t(reinterpret_cast<std::uintptr_t>(&mesh));}
    auto findSource(const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const std::vector<std::uint32_t>& key){
        const auto& id=identity(*owner);auto range=entries_.equal_range(sourceHash(*owner));
        for(auto it=range.first;it!=range.second;++it){auto& e=it->second;
            if(e.key!=key)continue;
            const bool same=e.sourceAddress==owner.get()&&sameOwner(e.owner,owner);
            if(id?!NorthlightSourceIdentity::same(id,e.identity):e.identity||!same)continue;
            if(id){++stats_.sourceIdentityHits;if(!same){++stats_.sourceReconstructedHits;
                // Only immutable source envelopes survive reconstruction. Pose/
                // camera results must be evaluated again for this live draw.
                releaseMemo(e);e.owner=owner;e.sourceAddress=owner.get();}}
            return it;
        }return entries_.end();
    }
    static constexpr std::uint64_t MaxIdleBuildFrames=8;
    static bool makeKey(const NorthlightActorDeformation::Program& p,const D3DVERTEXELEMENT9* elements,size_t count,std::vector<std::uint32_t>& key){
        if(!detail::linear(p)||!elements||!count||count>MAXD3DDECLLENGTH+1||p.inputs.size()>16||p.definitions.size()>256)return false;
        key.clear();auto word=[&](std::uint32_t n){key.push_back(n);};
        word(p.major);word(p.positionRegister);word(p.textureCoordinates);word(p.skinned);word(unsigned(p.operations.size()));
        for(const auto& op:p.operations){word(op.code);word(op.destination);for(const auto& s:op.source){word(s.token);word(s.address);}}
        word(unsigned(p.inputs.size()));for(const auto& i:p.inputs){word(i.reg);word(i.usage);word(i.index);}
        word(unsigned(p.definitions.size()));for(const auto& d:p.definitions){word(d.reg);for(float f:d.value){std::uint32_t bits;std::memcpy(&bits,&f,4);word(bits);}}
        word(unsigned(count));for(size_t j=0;j<count;++j){const auto& e=elements[j];word(e.Stream);word(e.Offset);word(e.Type);word(e.Method);word(e.Usage);word(e.UsageIndex);}
        return true;
    }
    static std::uint64_t programHash(const NorthlightActorDeformation::Program& p){
        // Diagnostic identity of the sliced program, not a replacement for the
        // exact full program/declaration key and immutable mesh ownership proof.
        std::uint64_t hash=1469598103934665603ull;auto word=[&](std::uint32_t n){hash^=n;hash*=1099511628211ull;};
        word(p.major);word(p.positionRegister);word(p.textureCoordinates);word(p.skinned);word(unsigned(p.operations.size()));
        for(const auto& o:p.operations){word(o.code);word(o.destination);for(const auto& src:o.source){word(src.token);word(src.address);}}
        word(unsigned(p.inputs.size()));for(const auto& i:p.inputs){word(i.reg);word(i.usage);word(i.index);}
        word(unsigned(p.definitions.size()));for(const auto& d:p.definitions){word(d.reg);for(float f:d.value){std::uint32_t bits;std::memcpy(&bits,&f,4);word(bits);}}
        return hash;
    }
    static bool prepare(const NorthlightActorDeformation::Program& p,const NorthlightDrawSnapshot::Mesh& mesh,const D3DVERTEXELEMENT9* elements,size_t count,Entry& e){
        if(!mesh.vertexCount||mesh.vertexCount>262144||!mesh.primitiveCount||mesh.primitiveCount>87380||
           (mesh.topology!=D3DPT_TRIANGLELIST&&mesh.topology!=D3DPT_TRIANGLESTRIP))return false;
        e.indexCount=mesh.topology==D3DPT_TRIANGLELIST?size_t(mesh.primitiveCount)*3:size_t(mesh.primitiveCount)+2;
        if(mesh.indexed){if(mesh.indices.size()<e.indexCount)return false;}else{if(e.indexCount>mesh.vertexCount)return false;e.index=e.indexCount;}
        unsigned used=0;
        for(const auto& input:p.inputs){if(input.reg>=16||(used&(1u<<input.reg)))return false;used|=1u<<input.reg;bool found=false;
            for(size_t j=0;j<count;++j){const auto& element=elements[j];if(element.Stream==0xff)break;
                if(element.Usage!=input.usage||element.UsageIndex!=input.index)continue;
                if(found||element.Stream>=4||element.Method!=D3DDECLMETHOD_DEFAULT)return false;
                const auto& s=mesh.streams[element.Stream];const auto n=NorthlightDrawSnapshot::declarationBytes(element.Type);
                if(!n||UINT(element.Offset)+n>s.stride||uint64_t(mesh.vertexCount)*s.stride>s.bytes.size())return false;
                e.bindings.push_back({input.reg,element});found=true;
            }if(!found)return false;
        }
        if(SkinEnvelope::supports(p))e.skin=std::make_unique<SkinEnvelope>();
        /* The skin template's own DEF is c0; its evaluator reads only palette
           rows c31..c255. Copying the 3.6 KiB bank then changes no value read. */
        e.copyDefinitions=!p.definitions.empty();
        if(e.skin){e.copyDefinitions=false;for(const auto& d:p.definitions)e.copyDefinitions|=d.reg>=31;}
        // The single-bone evaluator resolves its c0 literal directly; an ordinary
        // palette needs no 4 KiB copy just to replace that control register.
        e.singleBone=p.kernel.valid&&p.kernel.ops.size()==p.operations.size()&&p.kernel.singleSkin();
        if(e.singleBone){
            e.copyDefinitions=false;for(const auto& d:p.definitions)e.copyDefinitions|=d.reg!=0;
        }
        // A superset of EVERY original API constant read by this evaluator.
        // The exact skin template reads palette rows c31..c255. Generic relative
        // addressing retains the full bank; no observed vertex narrows it.
        if(e.skin){e.constantFirst=31;e.constantCount=225;}
        else {
            unsigned first=256,end=0;bool relative=false;
            for(const auto& op:p.operations){const unsigned count=unsigned(NorthlightShadowShader::arithmeticOperands(op.code,p.major)-1);
                for(unsigned j=0;j<count;++j){const auto& src=op.source[j];if(NorthlightShadowShader::regType(src.token)!=2)continue;
                    if(src.token&0x2000){relative=true;continue;}
                    const unsigned reg=NorthlightShadowShader::regIndex(src.token);if(reg>=256)return false;
                    bool defined=false;for(const auto& def:p.definitions)if(def.reg==reg){defined=true;break;}
                    if(!defined){first=std::min(first,reg);end=std::max(end,reg+1);}
                }
            }
            e.constantFirst=relative?0:end?first:0;e.constantCount=relative?256:end?end-first:0;
        }
        return true;
    }
    void releaseMemo(size_t slot){
        auto& memo=memo_[slot];if(!memo.serial)return;
        bytes_-=memo.bytes;memoBytes_-=memo.bytes;--memoEntries_;++stats_.memoEvictions;
        memo.serial=0;memo.lastUsedFrame=0;memo.bytes=0;memo.bounds={};std::vector<float>().swap(memo.key);
    }
    void releaseMemo(const Entry& entry){
        if(entry.memoSlot<memo_.size()&&memo_[entry.memoSlot].serial==entry.serial)releaseMemo(entry.memoSlot);
    }
    void trimMemos(size_t totalExtra,size_t memoExtra=0){
        for(size_t n=0;n<memo_.size()&&(totalExtra>Limit-bytes_||memoExtra>MemoLimit-memoBytes_);++n)
            releaseMemo((memoNext_+n)%memo_.size());
        // Empty optional tables must not displace a source envelope either.
        if(!memoEntries_&&memoTableBytes_&&(totalExtra>Limit-bytes_||memoExtra>MemoLimit-memoBytes_)){
            bytes_-=memoTableBytes_;memoBytes_-=memoTableBytes_;memoTableBytes_=0;std::vector<Memo>().swap(memo_);memoNext_=0;
        }
    }
    bool memoHit(const Entry& entry,const float* constants,const float* inverse,Bounds& output){
        if(!ResultMemo||!memoPolicy_.enabled()){++stats_.memoBypassed;return false;}
        ++stats_.memoTests;
        if(entry.memoSlot<memo_.size()){
            auto& memo=memo_[entry.memoSlot];const size_t values=size_t(entry.constantCount)*4;
            if(memo.serial==entry.serial&&memo.key.size()==values+16){
                memo.lastUsedFrame=frame_;
                // Reject a moving camera before comparing up to 4 KiB of pose.
                // Report the first failed proof; equality still requires BOTH.
                stats_.memoComparedBytes+=16*sizeof(float);
                if(std::memcmp(memo.key.data()+values,inverse,16*sizeof(float)))++stats_.memoCameraMisses;
                else {
                    stats_.memoComparedBytes+=values*sizeof(float);
                    if(std::memcmp(memo.key.data(),constants+4*entry.constantFirst,values*sizeof(float)))++stats_.memoConstantMisses;
                    else {output=memo.bounds;return true;}
                }
            }else ++stats_.memoReplacedMisses;
        }else ++stats_.memoColdMisses;
        ++stats_.memoMisses;return false;
    }
    void remember(Entry& entry,const float* constants,const float* inverse,const Bounds& output){
        if(!output.valid||!entry.serial)return;
        if(!ResultMemo||!memoPolicy_.enabled()){++stats_.memoStoreBypassed;return;}
        // Optional memo failure cannot discard a successfully computed bound.
        try {
            if(memo_.empty()){
                const size_t table=MemoSlots*sizeof(Memo)+32;
                if(table>Limit-bytes_){++stats_.memoRejected;return;}
                memo_.resize(MemoSlots);memoTableBytes_=memo_.capacity()*sizeof(Memo)+32;
                bytes_+=memoTableBytes_;memoBytes_+=memoTableBytes_;
            }
            const size_t values=size_t(entry.constantCount)*4,keyBytes=(values+16)*sizeof(float)+32;
            size_t slot=entry.memoSlot<memo_.size()&&memo_[entry.memoSlot].serial==entry.serial?entry.memoSlot:memoNext_++%memo_.size();
            // A long sequential scan must not evict the useful working set on
            // every frame. Probe ONE rotating victim, in constant time. A new
            // source may replace it only after an entire frame without use.
            // Capacity trimming may still reclaim any optional memo first.
            if(memoProtect_&&memo_[slot].serial&&memo_[slot].serial!=entry.serial&&frame_-memo_[slot].lastUsedFrame<=1){
                ++stats_.memoProtected;return;
            }
            // A miss usually changes the pose, not key size. Reuse the bounded
            // slot allocation instead of allocating/freeing a bank per draw.
            if(memo_[slot].serial&&memo_[slot].key.capacity()>=values+16){
                auto& memo=memo_[slot];if(memo.serial!=entry.serial)++stats_.memoEvictions;
                memo.key.resize(values+16);
                if(values)std::memcpy(memo.key.data(),constants+4*entry.constantFirst,values*sizeof(float));
                std::memcpy(memo.key.data()+values,inverse,16*sizeof(float));
                memo.bounds=output;memo.serial=entry.serial;memo.lastUsedFrame=frame_;entry.memoSlot=unsigned(slot);
                ++stats_.memoStored;stats_.memoCopiedBytes+=(values+16)*sizeof(float);return;
            }
            releaseMemo(slot);trimMemos(keyBytes,keyBytes);
            // trimMemos may free an empty table under severe source pressure.
            if(memo_.empty()||keyBytes>Limit-bytes_||keyBytes>MemoLimit-memoBytes_){++stats_.memoRejected;return;}
            std::vector<float> key(values+16);
            if(values)std::memcpy(key.data(),constants+4*entry.constantFirst,values*sizeof(float));
            std::memcpy(key.data()+values,inverse,16*sizeof(float));
            const size_t actual=key.capacity()*sizeof(float)+32;
            if(actual>Limit-bytes_||actual>MemoLimit-memoBytes_){++stats_.memoRejected;return;}
            auto& memo=memo_[slot];memo.key=std::move(key);memo.bounds=output;memo.serial=entry.serial;memo.bytes=actual;
            memo.lastUsedFrame=frame_;stats_.memoCopiedBytes+=(values+16)*sizeof(float);
            bytes_+=actual;memoBytes_+=actual;++memoEntries_;entry.memoSlot=unsigned(slot);++stats_.memoStored;
        }catch(...){++stats_.memoRejected;}
    }
    void discardGroups(Entry& e){
        if(pending_==&e)pending_=nullptr;releaseMemo(e);
        const size_t released=e.bytes-e.baseBytes;std::unordered_map<std::string,Group>().swap(e.groups);e.skin.reset();
        e.bytes=e.baseBytes;bytes_-=released;stats_.releasedGroupBytes+=released;e.unsupported=true;e.ready=false;
    }
    bool evict(const Entry* protectedEntry=nullptr){
        auto oldest=entries_.end();int best=99;
        for(auto it=entries_.begin();it!=entries_.end();++it){auto& e=it->second;if(&e==protectedEntry||&e==pending_)continue;
            const int priority=!e.identity&&e.owner.expired()?0:e.unsupported?1:!e.ready?2:3;
            if(oldest==entries_.end()||priority<best||(priority==best&&e.touched<oldest->second.touched)){oldest=it;best=priority;if(priority==0)break;}
        }
        if(oldest!=entries_.end()){++stats_.evictions;stats_.readyEvictions+=oldest->second.ready;stats_.expiredEvictions+=oldest->second.owner.expired();releaseMemo(oldest->second);bytes_-=oldest->second.bytes;entries_.erase(oldest);return true;}
        return false;
    }
    struct Timer {
        using Clock=BoundsClock;
        EnvelopeCache& cache;bool building;bool heavy=false,sliced=false;
        Clock::time_point start=Clock::now(),checked=start,sliceStart=start,evaluationStart=start;
        Timer(EnvelopeCache& owner,bool build,bool expensive=false):cache(owner),building(build),heavy(expensive){cache.flushFast(start);}
        Entry* evaluated=nullptr;WorkInfo* info=nullptr;Stage stage=Stage::All;Status* result=nullptr;size_t groups=0,operations=0;
        static std::uint64_t delta(Clock::time_point a,Clock::time_point b){return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(a-b).count());}
        std::uint64_t& bank(){return building?cache.buildNanoseconds_:heavy?cache.heavyNanoseconds_:cache.readyNanoseconds_;}
        void charge(Clock::time_point now){bank()+=delta(now,start);start=checked=now;}
        void mode(bool next){if(next!=building){charge(Clock::now());building=next;}}
        void slice(){sliced=true;sliceStart=checked;}
        bool sliceExpired()const{return sliced&&delta(checked,sliceStart)>=CheapSliceNanoseconds;}
        bool expired(){checked=Clock::now();return sliceExpired()||bank()+delta(checked,start)>=(building?cache.timeBudget_.buildLimit:heavy?HeavyNanoseconds:cache.timeBudget_.cheapLimit);}
        void evaluation(Entry& e,WorkInfo* metadata,Stage phase,Status& status,size_t groupCount,size_t operationCount){
            evaluated=&e;info=metadata;stage=phase;result=&status;groups=groupCount;operations=operationCount;evaluationStart=checked;
        }
        ~Timer(){
            const auto now=Clock::now();
            if(evaluated){const auto ns=delta(now,evaluationStart);evaluated->lastEvaluationNs=ns;
                if(cache.diagnostics_){if(info)info->elapsedUs=double(ns)/1000;
                    if(stage==Stage::Heavy||(stage==Stage::Cheap&&*result==Status::Budget))
                        cache.expensive_={true,cache.frame_,evaluated->programHash,groups,operations,double(ns)/1000,stage,*result,bool(evaluated->skin)};
                }
            }
            charge(now);
        }
    };
    Status buildEntry(Entry& e,const NorthlightDrawSnapshot::Mesh& mesh,Budget& budget,Timer& timer){
        // Budget interruptions retain only partial SOURCE envelopes. They can
        // never set a draw's valid flag; evaluation still uses its current pose.
        struct Progress {
            EnvelopeCache& cache;Entry& entry;size_t index,vertex;
            ~Progress(){if(cache.pending_==&entry&&(entry.index!=index||entry.vertex!=vertex))cache.pendingProgressFrame_=cache.frame_;}
        } progress{*this,e,e.index,e.vertex};
        while(e.index<e.indexCount){if(timer.expired())return timeBudget();
            if(budget.operations>=budget.maxOperations){++stats_.operationDeferred;return Status::Budget;}
            const size_t end=std::min(e.indexCount,e.index+64);
            if(end-e.index>budget.maxOperations-budget.operations){++stats_.operationDeferred;return Status::Budget;}
            budget.operations+=end-e.index;stats_.buildIndices+=end-e.index;
            for(;e.index<end;++e.index)if(mesh.indices[e.index]>=mesh.vertexCount){discardGroups(e);++stats_.invalid;return Status::Invalid;}
        }
        for(unsigned sinceCheck=0;e.vertex<mesh.vertexCount;++sinceCheck){
            if(budget.vertices>=budget.maxVertices){++stats_.vertexDeferred;return Status::Budget;}
            if((sinceCheck%BuildClockStride)==0&&timer.expired())return timeBudget();NorthlightActorDeformation::Four inputs[16];std::string groupKey;
            for(const auto& binding:e.bindings){const auto& s=mesh.streams[binding.element.Stream];auto& input=inputs[binding.reg];
                if(!NorthlightActorDeformation::decodeElement(s.bytes.data()+e.vertex*s.stride+binding.element.Offset,binding.element.Type,input)){discardGroups(e);++stats_.invalid;return Status::Invalid;}
                for(float f:input)if(!std::isfinite(f)){discardGroups(e);++stats_.invalid;return Status::Invalid;}
                // Audited single-bone programs address only v1.x. Unused YZW
                // must not split one bone into hundreds of cold groups.
                if(!e.skin&&binding.element.Usage==2)groupKey.append(reinterpret_cast<const char*>(input.data()),
                    e.singleBone&&binding.reg==1?4:16);
            }
            if(e.skin){if(!e.skin->add(inputs)){discardGroups(e);++stats_.unsupported;return Status::Unsupported;}}
            else {
                auto group=e.groups.find(groupKey);
                if(group==e.groups.end()){
                    if(e.groups.size()>=MaxGroups){discardGroups(e);++stats_.tupleLimit;++stats_.unsupported;return Status::Unsupported;}
                    Group g;g.limits.resize(e.bindings.size()*8);const size_t added=sizeof(Group)+g.limits.capacity()*4+groupKey.capacity()+1+192;
                    trimMemos(added);while(entries_.size()>1&&bytes_+added>Limit){if(!evict(&e))break;}if(bytes_+added>Limit)return Status::Budget;
                    for(size_t i=0;i<e.bindings.size();++i)for(unsigned j=0;j<4;++j)g.limits[i*8+j]=g.limits[i*8+4+j]=inputs[e.bindings[i].reg][j];
                    group=e.groups.emplace(std::move(groupKey),std::move(g)).first;e.bytes+=added;bytes_+=added;
                }else for(size_t i=0;i<e.bindings.size();++i)for(unsigned j=0;j<4;++j){auto& g=group->second;const float f=inputs[e.bindings[i].reg][j];g.limits[i*8+j]=std::min(g.limits[i*8+j],f);g.limits[i*8+4+j]=std::max(g.limits[i*8+4+j],f);}
            }
            ++e.vertex;++budget.vertices;++stats_.buildVertices;
        }
        e.ready=true;pending_=nullptr;++stats_.completed;
        return Status::Valid;
    }
    Status timeBudget(){++stats_.timeDeferred;return Status::Budget;}
public:
    EnvelopeCache()=default;
    EnvelopeCache(const EnvelopeCache&)=delete;EnvelopeCache& operator=(const EnvelopeCache&)=delete;
    EnvelopeCache(EnvelopeCache&&)=delete;EnvelopeCache& operator=(EnvelopeCache&&)=delete;
    void clear(){pending_=nullptr;pendingProgressFrame_=0;std::vector<Memo>().swap(memo_);memoBytes_=memoTableBytes_=memoNext_=memoEntries_=0;memoPolicy_={};memoTotals_={};entrySerial_=0;entries_.clear();scratch_.clear();bytes_=0;clock_=frame_=0;expensive_={};stats_={};beginFrame();}
    void beginFrame(bool diagnostics=true,size_t packets=0){
        fastOpen_=false;fastCount_=0;
        memoTotals_.add(stats_);
        memoPolicy_.beginFrame(stats_.memoTests,stats_.memoHits);
        memoProtect_=true;
        diagnostics_=diagnostics;timeBudget_.reset(packets);++frame_;readyNanoseconds_=heavyNanoseconds_=buildNanoseconds_=classifyNanoseconds_=0;readyCalls_=heavyCalls_=buildCalls_=0;stats_={};
        if(pending_&&pending_->owner.expired()){pending_=nullptr;++stats_.pendingOwnerExpired;}
        else if(pending_&&frame_-pendingProgressFrame_>MaxIdleBuildFrames){pending_=nullptr;++stats_.pendingAbandoned;}
    }
    // Call before the ordinary cold scan, using the SAME frame build budget.
    // Continuing is independent of packet cursor/classification and never reads
    // a pose, evaluates bounds, or consumes cheap/heavy evaluation allowances.
    void continuePendingBuild(Budget& budget){
        if(!pending_||!canBuild()||budget.vertices>=budget.maxVertices||budget.operations>=budget.maxOperations)return;
        ++buildCalls_;Timer timer{*this,true};
        auto owner=pending_->owner.lock();
        if(!owner){pending_=nullptr;++stats_.pendingOwnerExpired;return;}
        ++stats_.pendingResumes;
        try{pending_->touched=++clock_;(void)buildEntry(*pending_,*owner,budget,timer);}
        catch(...){pending_=nullptr;++stats_.pendingAbandoned;}
    }
    bool hasPendingBuild()const{return pending_!=nullptr;}
    size_t pendingBuildVertices()const{return pending_?pending_->vertex:0;}
    size_t pendingBuildIndices()const{return pending_?pending_->index:0;}
    void settleClock(){flushFast(BoundsClock::now());}
    void lendToBuild(Budget& budget){
        settleClock();timeBudget_.lendToBuild(readyNanoseconds_,heavyNanoseconds_);
        budget.maxVertices=std::max(budget.maxVertices,timeBudget_.buildVertices());
    }
    void finishReservedTurnsAndLend(){
        settleClock();
        timeBudget_.finishReservedTurns(heavyNanoseconds_,buildNanoseconds_);
        // The completion pass skips packets whose bounds are already valid.
        // Let its remaining sources replace earlier results: retaining them
        // would pin slots for packets that this pass no longer visits.
        memoProtect_=false;
    }
    void chargeCheapPreparation(std::uint64_t ns)noexcept{readyNanoseconds_+=std::min(ns,std::numeric_limits<std::uint64_t>::max()-readyNanoseconds_);}
    double borrowedMicroseconds()const{return double(timeBudget_.lent())/1000.;}
    bool canReady()const{return readyCalls_<4096&&readyNanoseconds_<timeBudget_.cheapLimit;}
    bool canHeavy()const{return !timeBudget_.reservedClosed&&heavyCalls_<2048&&heavyNanoseconds_<HeavyNanoseconds;}
    bool canBuild()const{return !timeBudget_.reservedClosed&&buildCalls_<2048&&buildNanoseconds_<timeBudget_.buildLimit;}
    bool canWork()const{return canReady()||canBuild();}
    // Time of a phase (0 cheap, 1 heavy, 2 build) spent so far this frame and its limit: the near walk of the bounds job takes half of what is left.
    std::uint64_t phaseNanoseconds(unsigned phase)const{return phase==0?readyNanoseconds_:phase==1?heavyNanoseconds_:buildNanoseconds_;}
    std::uint64_t phaseLimit(unsigned phase)const{return phase==0?timeBudget_.cheapLimit:phase==1?HeavyNanoseconds:timeBudget_.buildLimit;}
    double readyMilliseconds()const{return double(readyNanoseconds_)/1000000.;}
    double heavyMilliseconds()const{return double(heavyNanoseconds_)/1000000.;}
    double classifyMilliseconds()const{return double(classifyNanoseconds_)/1000000.;}
    const Expensive& lastExpensive()const{return expensive_;}
    double buildMilliseconds()const{return double(buildNanoseconds_)/1000000.;}
    double workMilliseconds()const{return readyMilliseconds()+heavyMilliseconds()+buildMilliseconds();}
    const Stats& stats()const{return stats_;}
    size_t entries()const{return entries_.size();}
    size_t reservedBytes()const{return bytes_;}
    size_t memoBytes()const{return memoBytes_;}
    size_t memoEntries()const{return memoEntries_;}
    const NorthlightReplayMemo::Policy& memoPolicy()const{return memoPolicy_;}
    MemoTotals memoTotals()const{auto total=memoTotals_;total.add(stats_);return total;}
    static std::shared_ptr<const Prepared> prepareProgram(const NorthlightActorDeformation::Program& program,const D3DVERTEXELEMENT9* elements,size_t count){
        try {std::shared_ptr<Prepared> prepared(new Prepared);
            if(!makeKey(program,elements,count,prepared->key_))return {};
            prepared->program_=program;prepared->declaration_.assign(elements,elements+count);prepared->hash_=programHash(program);
            return prepared;
        }catch(...){return {};}
    }
    Status calculatePrepared(const Prepared& prepared,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const float* constants,const float* inverse,
        Budget& budget,Bounds& output,Stage stage=Stage::All,WorkInfo* info=nullptr){
        return calculateImpl(prepared.program_,mesh,owner,prepared.declaration_.data(),prepared.declaration_.size(),constants,inverse,budget,output,stage,info,&prepared);
    }
    Status calculate(const NorthlightActorDeformation::Program& p,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const D3DVERTEXELEMENT9* elements,size_t count,
        const float* constants,const float* inverse,Budget& budget,Bounds& output,Stage stage=Stage::All,WorkInfo* info=nullptr){
        return calculateImpl(p,mesh,owner,elements,count,constants,inverse,budget,output,stage,info,nullptr);
    }
private:
    const float* effectiveConstants(const NorthlightActorDeformation::Program& p,const Entry& e,const float* captured,float* storage){
        if(!e.copyDefinitions)return captured;
        const size_t copied=size_t(e.constantCount)*16;
        if(copied)std::memcpy(storage+4*e.constantFirst,captured+4*e.constantFirst,copied);
        stats_.effectiveCopiedBytes+=copied;
        for(const auto& def:p.definitions){if(def.reg>=256)return nullptr;std::memcpy(storage+4*def.reg,def.value.data(),16);}
        return storage;
    }
    template<class Stop> static Status evaluateGroups(const NorthlightActorDeformation::Program& p,const Entry& e,
        const float* constants,const float* inverse,Budget& budget,Bounds& output,Stop&& stop){
        double low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
        for(const auto& pair:e.groups){
            if(stop())return Status::Budget;
            budget.operations+=p.operations.size();detail::Vector inputs[16]={};
            const auto& limits=pair.second.limits;
            for(size_t i=0;i<e.bindings.size();++i)for(unsigned j=0;j<4;++j){const float lo=limits[i*8+j],hi=limits[i*8+4+j],mid=float((double(lo)+hi)*.5);inputs[e.bindings[i].reg][j]={mid,std::max(double(mid)-lo,double(hi)-mid)};}
            detail::Vector position;
            if(!detail::singleSkinIntervals(p,inputs,constants,position,e.copyDefinitions)&&!detail::evaluateIntervals(p,inputs,constants,position,e.copyDefinitions))return Status::Unsupported;
            for(unsigned axis=0;axis<3;++axis){detail::Value world=detail::mul(position[3],{inverse[12+axis],0});double magnitude=std::fabs(world.value)+world.error;
                for(unsigned k=0;k<3;++k){auto term=detail::mul(position[k],{inverse[4*k+axis],0});magnitude+=std::fabs(term.value)+term.error;world=detail::add(world,term);}
                world.error+=detail::roundError(magnitude)*8+.001;
                if(!std::isfinite(world.value)||!std::isfinite(world.error)||std::fabs(world.value)+world.error>1000000)return Status::Unsupported;
                low[axis]=std::min(low[axis],double(world.value)-world.error);high[axis]=std::max(high[axis],double(world.value)+world.error);
            }
        }
        for(unsigned axis=0;axis<3;++axis){output.low[axis]=std::nextafter(float(low[axis]),-std::numeric_limits<float>::infinity());output.high[axis]=std::nextafter(float(high[axis]),std::numeric_limits<float>::infinity());}
        output.valid=true;return Status::Valid;
    }
    // Warm cheap skins and small generic envelopes share the batched clock.
    // Cold/heavy work retains its interruptible Timer path and reserved budget.
    bool fastReady(const Prepared& prepared,const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const float* constants,const float* inverse,
        Budget& budget,Bounds& output,WorkInfo* info,Status& status,Timer* individual=nullptr){
        const bool batched=!individual;
        auto found=findSource(owner,prepared.key_);Entry* e=found==entries_.end()?nullptr:&found->second;
        if(!e||e->unsupported||!e->ready||(e->skin&&e->copyDefinitions)||e->lastEvaluationNs>=CheapSliceNanoseconds)return false;
        const auto& p=prepared.program_;
        const size_t groups=e->skin?e->skin->usedBones:e->groups.size();
        const size_t operations=e->skin?size_t(75)+groups*16:groups*p.operations.size();
        if((!e->skin||!batched)&&(groups>8||operations>128))return false;
        if(budget.operations>budget.maxOperations||operations>budget.maxOperations-budget.operations)return false;
        for(unsigned j=0;j<16;++j)if(!std::isfinite(inverse[j]))return false;
        if(inverse[3]!=0||inverse[7]!=0||inverse[11]!=0||inverse[15]!=1)return false;
        if(batched&&!fastOpen_){fastMark_=BoundsClock::now();fastOpen_=true;fastCount_=0;}
        e->touched=++clock_;++stats_.memoBypassed;
        if(info){info->kind=Kind::Cheap;info->programHash=e->programHash;info->groups=groups;info->operations=operations;info->skin=bool(e->skin);}
        if(individual)individual->evaluation(*e,info,Stage::All,status,groups,operations);
        float storage[1024];const float* effective=effectiveConstants(p,*e,constants,storage);
        if(!effective){++stats_.invalid;status=Status::Invalid;}
        else if(e->skin){budget.operations+=operations;status=e->skin->evaluate(effective,inverse,output)?Status::Valid:Status::Unsupported;}
        else status=evaluateGroups(p,*e,effective,inverse,budget,output,[]{return false;});
        if(status==Status::Valid){++stats_.ready;stats_.skinValid+=bool(e->skin);stats_.cheapValid+=batched;++stats_.memoStoreBypassed;++stats_.fastValid;}
        else {output={};if(status==Status::Unsupported)++stats_.unsupported;}
        if(batched&&++fastCount_>=ReadyClockStride){const auto now=BoundsClock::now();readyNanoseconds_+=elapsedNs(now,fastMark_);fastMark_=now;fastCount_=0;}
        return true;
    }
    Status calculateImpl(const NorthlightActorDeformation::Program& p,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const D3DVERTEXELEMENT9* elements,size_t count,
        const float* constants,const float* inverse,Budget& budget,Bounds& output,Stage stage,WorkInfo* info,const Prepared* prepared){
        output={};if(info)*info={};
        if(!owner||owner.get()!=&mesh||!constants||!inverse){if(info)info->kind=Kind::Unsupported;return Status::Unsupported;}
        if(stage==Stage::Build?!canBuild():stage==Stage::Heavy?!canHeavy():!canReady())return timeBudget();
        if(stage==Stage::Build)++buildCalls_;else if(stage==Stage::Heavy)++heavyCalls_;else ++readyCalls_;
        if(FastReady&&!ResultMemo&&stage==Stage::Cheap&&prepared){Status fast;if(fastReady(*prepared,owner,constants,inverse,budget,output,info,fast))return fast;}
        // Material draws interleave unrelated game work. Time each bounded
        // warm evaluation separately; the shadow pass's batched clock would
        // charge those gaps and exhaust the material budget prematurely.
        if(FastReady&&!ResultMemo&&stage==Stage::All&&prepared){
            {Status fast;Timer timer{*this,false};if(fastReady(*prepared,owner,constants,inverse,budget,output,info,fast,&timer))return fast;}
            if(!canReady())return timeBudget();
        }
        Status evaluationStatus=Status::Budget;
        Timer timer{*this,stage==Stage::Build,stage==Stage::Heavy};
        const auto lookupStart=timer.start;bool classified=false;
        const auto accountClassify=[&](){if(diagnostics_&&!classified){classifyNanoseconds_+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(BoundsClock::now()-lookupStart).count());classified=true;}};
        try {
            if(!prepared&&!makeKey(p,elements,count,scratch_)){accountClassify();if(info)info->kind=Kind::Unsupported;++stats_.unsupported;return Status::Unsupported;}
            const auto& exactKey=prepared?prepared->key_:scratch_;
            const auto hash=sourceHash(mesh);auto found=findSource(owner,exactKey);
            accountClassify();
            if(found==entries_.end()){
                ++stats_.cold;if(info)info->kind=Kind::Cold;
                if(stage==Stage::Ready||stage==Stage::Cheap||stage==Stage::Heavy)return Status::Budget;
                timer.mode(true);if(!canBuild()||timer.expired())return timeBudget();
                if(pending_){++stats_.pendingDeferred;return Status::Budget;}
                Entry e;e.owner=owner;e.sourceAddress=owner.get();e.identity=identity(mesh);e.key=exactKey;e.programHash=prepared?prepared->hash_:programHash(p);if(!prepare(p,mesh,elements,count,e)){++stats_.invalid;return Status::Invalid;}
                e.baseBytes=sizeof(Entry)+e.key.capacity()*4+e.bindings.capacity()*sizeof(Binding)+256+(e.identity?e.identity->bytes():0);
                e.bytes=e.baseBytes+(e.skin?sizeof(SkinEnvelope)+32:0);if(e.bytes>Limit)return Status::Unsupported;
                trimMemos(e.bytes);
                while(!entries_.empty()&&(bytes_+e.bytes>Limit||entries_.size()>=MaxEntries)){if(!evict())return Status::Budget;}
                if(entrySerial_!=std::numeric_limits<std::uint64_t>::max())e.serial=++entrySerial_;
                const auto added=e.bytes;found=entries_.emplace(hash,std::move(e));bytes_+=added;
            }
            Entry& e=found->second;
            // Negative entries remain small and do not displace useful ready
            // entries merely because the same unsupported draw repeats often.
            if(e.unsupported){if(info)info->kind=Kind::Unsupported;++stats_.unsupported;return Status::Unsupported;}
            e.touched=++clock_;
            if(!e.ready){
                if(stage==Stage::Ready||stage==Stage::Cheap||stage==Stage::Heavy){++stats_.cold;if(info)info->kind=Kind::Cold;return Status::Budget;}
                timer.mode(true);if(!canBuild()||timer.expired())return timeBudget();
                if(pending_&&pending_!=&e){++stats_.pendingDeferred;return Status::Budget;}
                if(!pending_){pending_=&e;pendingProgressFrame_=frame_;}
                const auto built=buildEntry(e,mesh,budget,timer);
                if(built!=Status::Valid)return built;
                if(stage==Stage::Build)return Status::Budget; // Classify/evaluate next frame; never bypass the heavy quota.
            }else if(stage==Stage::Build){++stats_.buildSkippedReady;return Status::Unsupported;} // No build needed; do not pin the build cursor.
            const size_t groups=e.skin?e.skin->usedBones:e.groups.size();
            const size_t operations=e.skin?size_t(75)+size_t(e.skin->usedBones)*16:e.groups.size()*p.operations.size();
            const bool cheap=(e.skin||(e.groups.size()<=8&&operations<=128))&&e.lastEvaluationNs<CheapSliceNanoseconds;
            if(info){info->kind=cheap?Kind::Cheap:Kind::Heavy;info->programHash=e.programHash;info->groups=groups;info->operations=operations;info->skin=bool(e.skin);}
            if(memoHit(e,constants,inverse,output)){
                if(timer.expired()){output={};++stats_.memoBudgetDeferred;return timeBudget();}
                ++stats_.memoHits;stats_.memoAvoidedOperations+=operations;
                if(info)info->kind=Kind::Cheap;
                ++stats_.ready;stats_.skinValid+=bool(e.skin);
                if(stage==Stage::Cheap)++stats_.cheapValid;if(stage==Stage::Heavy)++stats_.heavyValid;
                return Status::Valid;
            }
            if(stage==Stage::Cheap&&!cheap){++stats_.heavyCandidates;return Status::Budget;}
            // Queued categories are hints only: exact current key/owner proof was
            // repeated above. A stale heavy hint never bypasses those checks.
            timer.mode(false);if((stage==Stage::Heavy?!canHeavy():!canReady())||timer.expired())return timeBudget();
            if(stage==Stage::Cheap)timer.slice();
            if(stage==Stage::Heavy)++stats_.heavyAttempts;
            timer.evaluation(e,info,stage,evaluationStatus,groups,operations);
            if(budget.operations>budget.maxOperations||operations>budget.maxOperations-budget.operations){++stats_.operationDeferred;return Status::Budget;}
            for(unsigned j=0;j<16;++j)if(!std::isfinite(inverse[j])){++stats_.invalid;evaluationStatus=Status::Invalid;return evaluationStatus;}
            if(inverse[3]!=0||inverse[7]!=0||inverse[11]!=0||inverse[15]!=1){++stats_.invalid;evaluationStatus=Status::Invalid;return evaluationStatus;}
            // Apply shader-local DEF overrides once, instead of searching their
            // list at every source fetch in every group. Captured banks untouched.
            float storage[1024];const float* effective=effectiveConstants(p,e,constants,storage);
            if(!effective){++stats_.invalid;evaluationStatus=Status::Invalid;return evaluationStatus;}
            if(e.skin){budget.operations+=operations;
                bool interrupted=false;
                struct StopContext {Timer* timer;bool* interrupted;};StopContext stop{&timer,&interrupted};
                const auto shouldStop=[](void* context){auto& s=*static_cast<StopContext*>(context);if(s.timer->expired()){*s.interrupted=true;return true;}return false;};
                if(!e.skin->evaluate(effective,inverse,output,shouldStop,&stop)){output={};
                    if(interrupted){if(stage==Stage::Cheap&&timer.sliceExpired()){++stats_.cheapSliceDeferred;if(info)info->kind=Kind::Heavy;}return timeBudget();}
                    ++stats_.unsupported;evaluationStatus=Status::Unsupported;return evaluationStatus;}
                ++stats_.ready;++stats_.skinValid;if(stage==Stage::Cheap)++stats_.cheapValid;if(stage==Stage::Heavy)++stats_.heavyValid;
                remember(e,constants,inverse,output);evaluationStatus=Status::Valid;return evaluationStatus;
            }
            evaluationStatus=evaluateGroups(p,e,effective,inverse,budget,output,[&]{return timer.expired();});
            if(evaluationStatus==Status::Budget){if(stage==Stage::Cheap&&timer.sliceExpired()){++stats_.cheapSliceDeferred;if(info)info->kind=Kind::Heavy;}return timeBudget();}
            if(evaluationStatus!=Status::Valid){++stats_.unsupported;return evaluationStatus;}
            ++stats_.ready;if(stage==Stage::Cheap)++stats_.cheapValid;if(stage==Stage::Heavy)++stats_.heavyValid;remember(e,constants,inverse,output);return evaluationStatus;
        }catch(...){evaluationStatus=Status::Unsupported;accountClassify();if(info)info->kind=Kind::Unsupported;++stats_.unsupported;return Status::Unsupported;}
    }
};
