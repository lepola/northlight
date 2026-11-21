#include "owned_scene_preparation.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <new>
#include <windows.h>

namespace water
{
OwnedScenePreparation::OwnedScenePreparation() : m_owner(std::this_thread::get_id()) {}

OwnedScenePreparation::~OwnedScenePreparation()
{
    assert(Owner());
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        m_cancelled.store(true);m_bakeCancelled.store(true);
        m_pending.reset();
    }
    m_wake.notify_one();
    // Shutdown only. No frame/Reset operation joins or destroys the executor.
    if (m_worker.joinable()) m_worker.join();
}

bool OwnedScenePreparation::Owner() const { return std::this_thread::get_id() == m_owner; }

void OwnedScenePreparation::SetContext(OwnedSceneIdentity identity, bool enabled)
{
    assert(Owner());
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_enabled == enabled && m_identity == identity) return;
        m_identity = identity;
        m_enabled = enabled;
        ++m_epoch; // Off/on and map/settings ABA transitions cannot revive an old result.
        m_cancelled.store(true);m_bakeCancelled.store(true);
        m_pending.reset();
    }
    m_wake.notify_one();
}

uint64_t OwnedScenePreparation::Submit(const OwnedFixtureDescription& description)
{
    assert(Owner());
    if (description.count == 0 || description.count > description.MaxBoxes ||
        description.delayMilliseconds > 10000) return 0;
    for (unsigned i = 0; i < description.count; ++i)
    {
        const auto& plane = description.boxes[i].plane;
        float length = 0;
        for (float value : plane) if (!std::isfinite(value)) return 0;
        for (unsigned axis = 0; axis < 3; ++axis) length += plane[axis] * plane[axis];
        if (std::fabs(length - 1) > .001f || plane[2] <= .25f) return 0;
        for (unsigned axis = 0; axis < 3; ++axis)
            if (!std::isfinite(description.boxes[i].centre[axis]) ||
                !std::isfinite(description.boxes[i].halfExtent[axis]) ||
                description.boxes[i].halfExtent[axis] <= 0 ||
                !std::isfinite(description.boxes[i].centre[axis] + description.boxes[i].halfExtent[axis]) ||
                !std::isfinite(description.boxes[i].centre[axis] - description.boxes[i].halfExtent[axis])) return 0;
    }
    Request request; request.fixture = description;
    return Queue(std::move(request));
}
uint64_t OwnedScenePreparation::SubmitWorld(const OwnedWorldDescription& description)
{
    assert(Owner());
    if (description.archiveList.empty() || description.archiveList.size() >= 32768 ||
        description.archiveList.find(L'\0') != std::wstring::npos || description.delayMilliseconds > 10000 ||
        description.texturePack.size()>=32768 || description.texturePack.find(L'\0')!=std::wstring::npos ||
        description.textureMipBias>1 || !description.sceneBytes || description.sceneBytes>world::SceneBytes ||
        description.textureCacheDirectory.size()>=32768 ||
        description.textureCacheDirectory.find(L'\0')!=std::wstring::npos ||
        (!description.texturePack.empty() && !description.textureCacheDirectory.empty()) ||
        description.textureCacheLimit<16*1024*1024 || description.textureCacheLimit>1024ull*1024*1024) return 0;
    try {
        Request request; request.world = std::make_shared<const OwnedWorldDescription>(description);
        return Queue(std::move(request));
    } catch (...) { return 0; }
}
uint64_t OwnedScenePreparation::Queue(Request request)
{
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock || !m_enabled) return 0;
    // Start only when used. No executor exists for the proven default producer.
    if (!m_worker.joinable()) {
        try { m_worker = std::thread(&OwnedScenePreparation::Run, this); }
        catch (...) { return 0; }
    }
    request.identity = m_identity; request.epoch = m_epoch; request.sequence = ++m_sequence;
    m_pending = std::move(request);m_bakeCancelled.store(true);
    m_wake.notify_one();
    return m_sequence;
}

OwnedPreparedScene OwnedScenePreparation::Read() const
{
    assert(Owner());
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    OwnedPreparedScene result;
    if (!lock) { result.state = OwnedPreparationState::Busy; return result; }
    result.pendingDescriptors = m_pending ? 1 : 0;
    result.bake=m_bakeStats;result.bake.pending=bool(m_bake);
    if(m_bake)result.replacementBytes=world::TextureStore::Workspace+m_bake->sceneBytes;
    if (m_phase == Phase::Idle) {
        result.replacementBytes += m_previousBytes;
        return result;
    }
    result.identity = m_current.identity;
    result.epoch = m_current.epoch;
    result.request = m_current.sequence;
    result.preparationMilliseconds=m_preparationMilliseconds;
    result.sourceBytes = m_sourceBytes.load(std::memory_order_acquire);
    result.replacementBytes = m_phase == Phase::Building ? (m_current.world ? WorldPreparationLimit : FixtureCpuLimit) :
                                                          (m_result ? m_result->preparedBytes : 0)+(m_bake ? world::TextureStore::Workspace : 0);
    result.state = OwnedPreparationState::Preparing;
    if (m_phase == Phase::Complete) {
        result.preparationThread = m_preparedOn;
        if (m_current.world) result.worldResult = &m_worldResult;
        result.state = !m_enabled || m_current.epoch != m_epoch ? OwnedPreparationState::Cancelled :
                       m_success ? OwnedPreparationState::Ready : OwnedPreparationState::Failed;
        if (result.state == OwnedPreparationState::Ready) result.scene = m_result.get();
    }
    return result;
}

bool OwnedScenePreparation::Retire()
{
    assert(Owner());
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock || m_phase != Phase::Complete) return false;
    // The slot stays occupied until disposal, including upload/publish time above us.
    m_result.reset();m_bakeCancelled.store(true);
    m_current.world.reset();
    m_phase = Phase::Idle;
    m_wake.notify_one();
    return true;
}

std::unique_ptr<const OwnedCpuScene> OwnedScenePreparation::Accept(bool replacing)
{
    assert(Owner());
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock || m_phase != Phase::Complete || !m_enabled || m_current.epoch != m_epoch || !m_success) return {};
    std::unique_ptr<const OwnedCpuScene> result = std::move(m_result);
    m_current.world.reset();
    m_phase = replacing ? Phase::Publishing : Phase::Idle;
    m_wake.notify_one();
    return result;
}

void OwnedScenePreparation::FinishReplacement(std::shared_ptr<const OwnedCpuScene> previous)
{
    assert(Owner() && previous);
    std::lock_guard<std::mutex> lock(m_mutex);
    assert(m_phase == Phase::Publishing && !m_previous && !m_previousBytes);
    m_previousBytes = previous->preparedBytes;
    m_previous = std::move(previous);
    m_phase = Phase::Idle;
    m_wake.notify_one();
}

bool OwnedScenePreparation::Prepare(const Request& request, OwnedCpuScene& scene,std::unique_ptr<BakeJob>& bake)
{
    m_worldResult = {};
    const auto delay = request.world ? request.world->delayMilliseconds : request.fixture.delayMilliseconds;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const auto cancelled = [&] { return m_stop || request.epoch != m_epoch; };
        if (m_wake.wait_for(lock, std::chrono::milliseconds(delay), cancelled)) return false;
    }
    if (request.world) {
        if (request.world->fail || (!request.world->streaming && request.identity.map != 0) ||
            (request.world->streaming && (!request.world->region.Valid() || request.world->region.map!=request.identity.map))) { m_worldResult.error = world::SceneError::Descriptor; return false; }
        const auto openedAt=std::chrono::steady_clock::now();
        auto worldScene = std::make_unique<world::WorldScene>();
        std::shared_ptr<const OwnedWorldRenderData> renderData;
        bool reuse=false;
        {
            const auto seed=request.world->reuse.lock();
            if(seed && seed->worldScene && seed->worldRender && seed->preparedBytes<=world::SceneBytes){
                // Asset catalogs can grow or promote a texture. Geometry and receiver data stay immutable.
                *worldScene=*seed->worldScene;
                renderData=seed->worldRender;
                reuse=true;
            }
        }
        if(m_cancelled.load())return false;
        world::ArchiveReader archives;
        m_worldResult.archiveError = archives.OpenList(request.world->archiveList, {}, m_cancelled);
        if (m_worldResult.archiveError != world::ArchiveError::None) {
            m_worldResult.error = m_worldResult.archiveError == world::ArchiveError::Cancelled ? world::SceneError::Cancelled :
                                  m_worldResult.archiveError == world::ArchiveError::Budget ? world::SceneError::Budget : world::SceneError::Archive;
            return false;
        }
        const auto assetsAt=std::chrono::steady_clock::now();
        world::SceneOptions options{request.world->buildings,request.world->texturePrefix,request.world->lake,request.world->snapshotReads,request.world->streaming,request.world->region,request.world->liveDisplays,request.world->textureMipBias,request.world->texturePack,request.world->textureCacheDirectory,request.world->populateTextures};
        options.shortDistance=request.world->shortDistance;
        world::SceneError reuseError=world::SceneError::None;
        const auto importWhole=[&]{
            worldScene=std::make_unique<world::WorldScene>();renderData.reset();
            m_worldResult=world::LoadRiverScene(archives,m_cancelled,*worldScene,request.world->sceneBytes,world::SceneWorkspaceBytes,&m_sourceBytes,options);
        };
        // What is given up, in turn, to fit the budget: the furthest models, which Northrend's dense regions need (more of them
        // than a scene can list), then the furthest buildings, nearest kept longest. Keeping a picture beats keeping no reflection.
        // Level 0 is full-size textures, for the fixed river scene only: a streamed region starts at half, which is hardly seen in a
        // reflection, because most regions needed it anyway and finding that out cost a whole failed import each time.
        constexpr float kFitModelReach[]={0,0,768,384,384,384},kFitBuildingReach[]={0,0,0,0,768,384};
        if(request.world->streaming)options.textureMipBias=1;
        // Short distance starts with the same bounded object import used for dense regions.
        const unsigned minimumFit=request.world->shortDistance?4u:(options.textureMipBias?1u:0u);
        unsigned fit=minimumFit;
        const auto fitAt=[&](unsigned level){fit=level;if(fit)options.textureMipBias=1;options.modelReach=kFitModelReach[fit];options.buildingReach=kFitBuildingReach[fit];};
        fitAt(minimumFit);
        const auto tighter=[&]{
            if(m_cancelled.load()||fit+1>=sizeof(kFitModelReach)/sizeof(kFitModelReach[0]))return false;
            fitAt(fit+1);return true;
        };
        const auto deriveWhole=[&]{
            auto data=std::make_shared<OwnedWorldRenderData>();
            const auto status=PrepareOwnedWorldRenderData(*worldScene,m_cancelled,*data,request.world->sceneBytes,world::SceneWorkspaceBytes,request.world->selectReceiver?request.world->receiver.data():nullptr);
            renderData=std::move(data);
            return status;
        };
        // A scene that was fitted with half-resolution textures extends at the same detail.
        if(reuse)options.textureMipBias=std::max(options.textureMipBias,world::SceneTextureMipBias(*worldScene));
        if(options.textureMipBias)fit=std::max(fit,1u);
        if(reuse){
            const auto derivedBytes=renderData->Bytes();
            if(derivedBytes>=request.world->sceneBytes){reuse=false;reuseError=world::SceneError::Budget;}
            else {
                m_worldResult=world::ExtendLiveScene(archives,m_cancelled,*worldScene,request.world->sceneBytes-derivedBytes,
                    world::SceneWorkspaceBytes-derivedBytes,&m_sourceBytes,options);
                reuse=m_worldResult.reusedScenery;reuseError=m_worldResult.error;
            }
            if(m_cancelled.load())return false;
        }
        if(!reuse){
            // Dispose the incomplete extension before allocating a complete
            // replacement. Source changes, material promotion failures and
            // display-slot pressure preserve the ordinary import fallback.
            // Near a region that needed models given up, start a step short of that: each failed try is seconds, and the first ones
            // were never going to fit.
            if(request.world->streaming && m_fitMap==request.world->region.map && m_fitLevel>1)fitAt(std::max(fit,m_fitLevel-1));
            importWhole();
            // A region whose full-detail textures exceed the scene budget (a crowded harbour plus
            // a ship) imports again with less rather than failing.
            while(m_worldResult.error==world::SceneError::Budget && tighter())importWhole();
            m_worldResult.reuseError=reuseError;
        }
        m_worldResult.textureMipBias=options.textureMipBias;m_worldResult.modelReach=options.modelReach;
        const auto derivedAt=std::chrono::steady_clock::now();
        m_worldResult.archive=archives.Stats();
        m_worldResult.archiveMilliseconds=std::chrono::duration<double,std::milli>(assetsAt-openedAt).count();
        m_worldResult.assetMilliseconds=std::chrono::duration<double,std::milli>(derivedAt-assetsAt).count();
        if (m_worldResult.error != world::SceneError::None) return false;
        auto prepared=OwnedWorldDataResult::Ready;
        if(reuse){
            const auto assets=worldScene->Bytes(),derived=renderData->Bytes();
            if(assets>request.world->sceneBytes || derived>request.world->sceneBytes-assets)
                prepared=OwnedWorldDataResult::Budget;
            else if(!PrepareOwnedTextureKeys(*worldScene,m_cancelled,true))
                prepared=m_cancelled.load()?OwnedWorldDataResult::Cancelled:OwnedWorldDataResult::Invalid;
        }else prepared=deriveWhole();
        // The budget holds the scene and what is derived from it together, so a scene can fit on its own and not with its
        // derived data (a region of many models). The same steps make the room here too; an extended scene that no longer fits
        // imports whole, as a region does.
        while(prepared==OwnedWorldDataResult::Budget && tighter()){
            reuse=false;importWhole();
            while(m_worldResult.error==world::SceneError::Budget && tighter())importWhole();
            m_worldResult.reuseError=reuseError;
            m_worldResult.textureMipBias=options.textureMipBias;m_worldResult.modelReach=options.modelReach;
            if(m_worldResult.error!=world::SceneError::None)return false;
            prepared=deriveWhole();
        }
        m_worldResult.derivedMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-derivedAt).count();
        if(prepared==OwnedWorldDataResult::Ready && !reuse){m_fitMap=request.world->region.map;m_fitLevel=request.world->shortDistance?0:fit;}
        if (prepared != OwnedWorldDataResult::Ready) {
            m_worldResult.error = prepared == OwnedWorldDataResult::Cancelled ? world::SceneError::Cancelled :
                                  prepared == OwnedWorldDataResult::Budget ? world::SceneError::Budget : world::SceneError::Internal;
            return false;
        }
        if(!reuse && options.streaming)PrepareOwnedWorldHorizon(archives,*worldScene,*renderData,m_cancelled,m_worldResult);
        if(m_cancelled.load())return false;
        m_worldResult.archiveError=archives.Validate();
        if(m_worldResult.archiveError!=world::ArchiveError::None){m_worldResult.error=world::SceneError::Archive;return false;}
        m_worldResult.peakWorkspaceBytes = std::max(m_worldResult.peakWorkspaceBytes, renderData->workspaceReservationBytes);
        scene.worldRender = std::move(renderData);
        scene.worldScene = std::move(worldScene);
        if(scene.Bytes()>WorldPreparationLimit)return false;
        if(request.world->populateTextures && !request.world->textureCacheDirectory.empty()) {
            // Optional bookkeeping cannot turn a completed source scene into failure.
            try {
                auto job=std::make_unique<BakeJob>();job->scene=scene.worldScene;
                job->root=request.world->textureCacheDirectory;job->limit=request.world->textureCacheLimit;
                job->identity=world::TextureCacheIdentity(archives,world::TextureRecipe());
                job->used=m_worldResult.textureCacheBuckets;
                for(auto index:m_worldResult.textureBakeCandidates) {
                    const auto& texture=job->scene->textures[index];
                    const auto origin=std::find_if(job->scene->sources.begin(),job->scene->sources.end(),[&](const auto& s){return s.name==texture.name;});
                    if(origin==job->scene->sources.end())continue;
                    unsigned ordinal=0;while(ordinal<archives.SourceCount() && archives.Source(ordinal)->path!=origin->archive.path)++ordinal;
                    if(ordinal==archives.SourceCount())continue;
                    const auto bucket=world::TextureStore::Bucket(texture.name);job->used|=uint64_t(1)<<bucket;
                    job->assets[bucket].push_back({{texture.name,texture.maxDimension,options.textureMipBias,
                        request.world->texturePrefix?world::TextureMips::RenderPrefix:world::TextureMips::StoredPrefix},ordinal,&texture.pixels.Read()});
                }
                if(job->used && !job->identity.empty())bake=std::move(job);
            } catch(...) { /* Source preparation and active scenery remain usable. */ }
        }
        return true;
    }
    if (request.fixture.fail) return false;
    // Eight shared corners and outward triangle winding for each box.
    constexpr uint16_t triangles[] = {0,2,1,1,2,3,4,5,6,5,7,6,0,1,4,1,5,4,
                                      2,6,3,3,6,7,0,4,2,2,4,6,1,3,5,3,7,5};
    scene.vertices.reserve(request.fixture.count * 8);
    scene.indices.reserve(request.fixture.count * 36);
    scene.draws.reserve(request.fixture.count);
    if (scene.Bytes() > FixtureCpuLimit) return false;
    for (unsigned i = 0; i < request.fixture.count; ++i) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stop || request.epoch != m_epoch) return false;
        }
        const auto& box = request.fixture.boxes[i];
        const auto base = static_cast<uint16_t>(scene.vertices.size());
        scene.draws.push_back({static_cast<unsigned>(scene.indices.size()), 36, box.plane});
        for (unsigned corner = 0; corner < 8; ++corner) {
            OwnedSceneVertex vertex = {{}, box.colour};
            for (unsigned axis = 0; axis < 3; ++axis)
                vertex.position[axis] = box.centre[axis] + ((corner >> axis) & 1 ? 1.f : -1.f) * box.halfExtent[axis];
            scene.vertices.push_back(vertex);
        }
        for (uint16_t index : triangles) scene.indices.push_back(base + index);
    }
    return true;
}

void OwnedScenePreparation::Run()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    for (;;) {
        m_wake.wait(lock, [&] { return m_stop || m_previous || (m_phase == Phase::Idle && m_pending) || bool(m_bake); });
        if (m_previous) {
            auto previous = std::move(m_previous);
            lock.unlock();
            previous.reset();
            lock.lock();
            m_previousBytes = 0;
            continue;
        }
        if (m_stop) return;
        if(m_bake && (m_pending || m_bakeCancelled.load())) {
            ++m_bakeStats.cancelled;m_bake.reset(); // release shared scene before any next source preparation
        }
        if(m_bake) {
            auto& job=*m_bake;
            while(job.next<world::TextureStore::Buckets && !(job.used&(uint64_t(1)<<job.next)))++job.next;
            if(job.next==world::TextureStore::Buckets){m_bake.reset();continue;}
            const auto bucket=job.next++;
            lock.unlock();
            const auto start=std::chrono::steady_clock::now();
            const bool background=SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN)!=FALSE;
            if(!background)SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_LOWEST);
            const auto outcome=world::TextureStore::Publish(job.root,job.identity,bucket,job.assets[bucket],job.limit,m_bakeCancelled);
            if(background)SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_END);
            else SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_NORMAL);
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            lock.lock();
            auto& stats=m_bakeStats;++stats.batches;stats.written+=outcome.written;stats.reused+=outcome.reused;
            stats.skipped+=outcome.skipped;stats.evicted+=outcome.evicted;stats.last=outcome.error;
            stats.diskBytes=outcome.diskBytes;stats.writtenBytes+=outcome.writtenBytes;
            stats.milliseconds+=ms;stats.maxBatchMilliseconds=std::max(stats.maxBatchMilliseconds,ms);
            if(outcome.error!=world::TexturePackError::None) {
                if(outcome.error!=world::TexturePackError::Cancelled)++stats.failures;
                else ++stats.cancelled;
                m_bake.reset(); // disk full, locked or unavailable: no per-frame retry loop
            }
            m_wake.wait_for(lock,std::chrono::milliseconds(50),[&]{return m_stop || m_previous || m_pending || m_bakeCancelled.load();});
            continue;
        }
        if(m_phase!=Phase::Idle || !m_pending)continue;
        m_current = *m_pending;
        m_pending.reset();
        m_phase = Phase::Building;
        m_cancelled.store(false);
        m_sourceBytes.store(0, std::memory_order_release);
        const auto request = m_current;
        m_preparationMilliseconds=0;
        lock.unlock();
        const auto preparationStart=std::chrono::steady_clock::now();
        std::unique_ptr<OwnedCpuScene> result;std::unique_ptr<BakeJob> bake;
        bool success = false;
        try {
            result = std::make_unique<OwnedCpuScene>();
            success = Prepare(request, *result,bake);
        } catch (const std::bad_alloc&) {
            if (request.world) m_worldResult.error = world::SceneError::Budget;
        } catch (...) {
            if (request.world) m_worldResult.error = world::SceneError::Internal;
        } // Even a partial CPU payload returns to the owner for disposal.
        if (result) result->preparedBytes = result->Bytes();
        if (bake) bake->sceneBytes = bake->scene->Bytes();
        const double preparationMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-preparationStart).count();
        lock.lock();
        m_preparationMilliseconds=preparationMs;
        if(success && !m_pending && m_enabled && request.epoch==m_epoch) {
            m_bake=std::move(bake);m_bakeCancelled.store(false);
        }
        m_result = std::move(result);
        m_success = success;
        m_preparedOn = std::this_thread::get_id();
        m_phase = Phase::Complete;
        // Do not consume another descriptor until the owner has retired this slot.
    }
}
}
