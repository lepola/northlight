#pragma once
#include "owned_world_render_data.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace water
{
struct OwnedCpuScene;
// CPU-only inputs. Device/frame stamps belong to submission, not scene residency.
struct OwnedSceneIdentity
{
    int32_t map = -1;
    uint64_t assets = 0, settings = 0;
    int32_t regionX=0,regionY=0;
    uint64_t liveAssets=0,receiver=0,preparation=0; // optional preparation policy; does not invalidate active geometry
    bool SameSource(const OwnedSceneIdentity& other) const
    { return map==other.map && assets==other.assets && settings==other.settings; }
    bool operator==(const OwnedSceneIdentity& other) const
    { return SameSource(other) &&
             regionX==other.regionX && regionY==other.regionY && liveAssets==other.liveAssets && receiver==other.receiver && preparation==other.preparation; }
};

struct OwnedFixtureBox
{
    std::array<float, 3> centre{}, halfExtent{};
    uint32_t colour = 0xffffffff;
    std::array<float, 4> plane{0, 0, 1, 0}; // unit world normal and plane distance
};

struct OwnedFixtureDescription
{
    static constexpr unsigned MaxBoxes = 8;
    std::array<OwnedFixtureBox, MaxBoxes> boxes{};
    unsigned count = 0;
    // Diagnostic controls travel with the request; no live settings are read by the worker.
    unsigned delayMilliseconds = 0;
    bool fail = false;
};
struct OwnedWorldDescription
{
    std::wstring archiveList;
    unsigned delayMilliseconds = 0;
    bool fail = false;
    bool buildings = false;
    bool texturePrefix = false;
    bool lake = false, snapshotReads=false;
    bool streaming=false;world::OwnedWorldRegion region;
    std::array<uint32_t,world::MaxLiveAssets> liveDisplays={};
    std::array<float,3> receiver={};
    bool selectReceiver=false;
    // Optional explicit pack for private acceptance; empty keeps source import.
    std::wstring texturePack={};
    unsigned textureMipBias=0;
    bool shortDistance=false;
    std::size_t sceneBytes=world::SceneBytes; // imported scene budget; at most that (lower in tests)
    std::wstring textureCacheDirectory={};
    bool populateTextures=false;
    uint64_t textureCacheLimit=256ull*1024*1024;
    std::weak_ptr<const OwnedCpuScene> reuse{}; // optional same-region seed; never pins an obsolete scene in the queue
};

struct OwnedSceneVertex
{
    float position[3];
    uint32_t colour;
};

struct OwnedCpuScene
{
    struct Draw
    {
        unsigned firstIndex = 0, indexCount = 0;
        std::array<float, 4> plane{};
    };
    std::vector<OwnedSceneVertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<Draw> draws;
    std::shared_ptr<const world::WorldScene> worldScene;
    std::shared_ptr<const OwnedWorldRenderData> worldRender;
    // The worker measures the finished payload before publishing it to the render thread.
    std::size_t preparedBytes = 0;
    std::size_t Bytes() const
    { return sizeof(*this) + vertices.capacity() * sizeof(OwnedSceneVertex) + indices.capacity() * sizeof(uint16_t) + draws.capacity() * sizeof(Draw) + (worldScene ? worldScene->Bytes() : 0) + (worldRender ? worldRender->Bytes() : 0); }
};

enum class OwnedPreparationState { Idle, Busy, Preparing, Ready, Failed, Cancelled };

struct OwnedTextureBakeStats
{
    uint64_t batches=0,written=0,reused=0,skipped=0,evicted=0,failures=0,cancelled=0,diskBytes=0,writtenBytes=0;
    double milliseconds=0,maxBatchMilliseconds=0;
    world::TexturePackError last=world::TexturePackError::None;
    bool pending=false;
};
struct OwnedPreparedScene
{
    OwnedTextureBakeStats bake;
    OwnedPreparationState state = OwnedPreparationState::Idle;
    OwnedSceneIdentity identity;
    uint64_t epoch = 0, request = 0;
    // Borrowed on the controller's owner until Retire/SetContext/destruction.
    // No worker can alter a completed payload while it is borrowed.
    const OwnedCpuScene* scene = nullptr;
    std::size_t replacementBytes = 0;
    unsigned pendingDescriptors = 0;
    const world::SceneResult* worldResult = nullptr; // same borrow lifetime as scene, including terminal failures
    std::thread::id preparationThread;
    double preparationMilliseconds = 0;
    uint64_t sourceBytes = 0; // worker progress only, not a readiness/coverage claim
};

// One replacement slot lasts through CPU completion, owner uploads and publication
// or discard. Retire explicitly releases it. Only one latest descriptor may wait.
// Every public operation, including destruction, is called by one controller owner.
// Neither input nor result contains a COM object, game address, callback or camera cull.
class OwnedScenePreparation
{
public:
    static constexpr std::size_t FixtureCpuLimit = 16 * 1024;
    // Conservative payload reservation while loading: scene/workspace, archive
    // tables (32 MiB), name cache (4 MiB) and bounded descriptors. This
    // excludes allocator/library overhead and separately held active residency.
    static constexpr std::size_t WorldPreparationLimit = 2 * world::SceneBytes + 8 * 1024 * 1024;
    OwnedScenePreparation();
    ~OwnedScenePreparation();
    OwnedScenePreparation(const OwnedScenePreparation&) = delete;
    OwnedScenePreparation& operator=(const OwnedScenePreparation&) = delete;

    void SetContext(OwnedSceneIdentity identity, bool enabled);
    uint64_t Submit(const OwnedFixtureDescription& description);
    uint64_t SubmitWorld(const OwnedWorldDescription& description);
    OwnedPreparedScene Read() const; // try-lock only; never waits for completion
    bool Retire();                  // completed payload is disposed on the owner
    // After successful GPU publication, transfer the same payload to active CPU residency.
    // The controller accounts that residency separately; no copy or completed queue is created.
    // A replacement holds the slot until the owner releases the old GPU resources
    // and passes their CPU backing to FinishReplacement.
    std::unique_ptr<const OwnedCpuScene> Accept(bool replacing = false);
    void FinishReplacement(std::shared_ptr<const OwnedCpuScene> previous);

private:
    struct Request
    {
        OwnedSceneIdentity identity;
        uint64_t epoch = 0, sequence = 0;
        OwnedFixtureDescription fixture;
        std::shared_ptr<const OwnedWorldDescription> world;
    };
    enum class Phase { Idle, Building, Complete, Publishing };
    bool Owner() const;
    uint64_t Queue(Request request);
    void Run();
    struct BakeJob {
        std::shared_ptr<const world::WorldScene> scene;
        std::size_t sceneBytes = 0;
        std::array<std::vector<world::TextureStore::Asset>,world::TextureStore::Buckets> assets;
        std::wstring root;
        std::string identity;
        uint64_t limit=0,used=0;
        unsigned next=0;
    };
    bool Prepare(const Request& request, OwnedCpuScene& scene,std::unique_ptr<BakeJob>& bake);

    const std::thread::id m_owner;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::thread m_worker;
    OwnedSceneIdentity m_identity;
    uint64_t m_epoch = 0, m_sequence = 0;
    bool m_enabled = false, m_stop = false, m_success = false;
    std::atomic<bool> m_cancelled{false},m_bakeCancelled{false};
    std::unique_ptr<BakeJob> m_bake;
    OwnedTextureBakeStats m_bakeStats;
    std::atomic<uint64_t> m_sourceBytes{0};
    Phase m_phase = Phase::Idle;
    std::optional<Request> m_pending;
    Request m_current;
    std::unique_ptr<OwnedCpuScene> m_result;
    std::shared_ptr<const OwnedCpuScene> m_previous;
    std::size_t m_previousBytes = 0; // includes disposal in progress outside the mutex
    world::SceneResult m_worldResult;
    double m_preparationMilliseconds=0;
    int32_t m_fitMap=-1;unsigned m_fitLevel=0; // worker only: how much the last whole region had to give up to fit
    std::thread::id m_preparedOn;
};
}
