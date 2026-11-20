#include "ground.h"
#include "ground_shift.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <system_error>
#include <exception>
#include "scenery_float_environment.h"

namespace
{
constexpr float kQueryAbove = 400.0f;
constexpr float kQueryBelow = 400.0f;
constexpr int kUntimedQueryBatch = 16;
constexpr int kCentre = GroundMap::kCells / 2;
constexpr int kLastRing = GroundMap::kCells / 2;
constexpr double kShoreRebuildSeconds = 0.25;
constexpr double kRetryMissingSeconds = 2.0;
}

// One bounded rebuild of storage owned by this GroundMap. The owner may read
// raw height inputs, but every mutation and every derived-data read resolves the
// job first. No game callbacks, device pointers, COM or engine data reach Run.
struct GroundMap::Worker {
    GroundMap& map;
    std::mutex mutex;std::condition_variable wake;std::thread thread;
    bool stop=false,queued=false,pending=false;
    std::atomic<bool> done{false};
    reflection::SceneryFloatEnvironment environment;
    bool rebuild=false;int shiftX=0,shiftY=0;
    double elapsed=0;std::exception_ptr failure;
    explicit Worker(GroundMap& owner):map(owner){thread=std::thread([this]{Run();});}
    ~Worker(){
        {std::lock_guard<std::mutex> lock(mutex);stop=true;}
        wake.notify_one();thread.join();
    }
    void Run(){
        for(;;){
            {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stop||queued;});
             if(stop)return;
             queued=false;}
            environment.Apply();const auto start=std::chrono::steady_clock::now();
            try{if(!rebuild && (shiftX||shiftY))map.ShiftDerived(shiftX,shiftY);
                if(rebuild)map.BuildShoreDistances();map.PackTexture();}catch(...){failure=std::current_exception();}
            elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            done.store(true,std::memory_order_release);wake.notify_all();
        }
    }
    void Submit(bool build,int dx,int dy){
        rebuild=build;shiftX=dx;shiftY=dy;
        failure=nullptr;environment.Capture();done.store(false,std::memory_order_relaxed);pending=true;
        {std::lock_guard<std::mutex> lock(mutex);queued=true;}
        wake.notify_one();
    }
};
namespace
{
uint64_t NextGroundIdentity()
{
    static std::atomic<uint64_t> identity{0};
    return identity.fetch_add(1, std::memory_order_relaxed) + 1;
}
}
GroundMap::GroundMap() : m_identity(NextGroundIdentity()) {}
GroundMap::GroundMap(float cellSize, bool retryMissing) : m_identity(NextGroundIdentity()), m_cell(cellSize), m_retryMissing(retryMissing) {}
GroundMap::GroundMap(const GroundMap& source, SnapshotTag) : m_identity(source.m_identity)
{
    CopySnapshotData(source);
}
void GroundMap::CopySnapshotData(const GroundMap& source)
{
    std::memcpy(m_height, source.m_height, sizeof(m_height));
    std::memcpy(m_liquid, source.m_liquid, sizeof(m_liquid));
    std::memcpy(m_shore, source.m_shore, sizeof(m_shore));
    std::memcpy(m_land, source.m_land, sizeof(m_land));
    std::memcpy(m_hot, source.m_hot, sizeof(m_hot));
    m_cell = source.m_cell;
    m_retryMissing = source.m_retryMissing;
    std::memcpy(m_liquidTrust, source.m_liquidTrust, sizeof(m_liquidTrust));
    std::memcpy(m_liquidNear, source.m_liquidNear, sizeof(m_liquidNear));
    std::memcpy(m_looked, source.m_looked, sizeof(m_looked));
    std::memcpy(m_origin, source.m_origin, sizeof(m_origin));
    m_nearbyWaterKnown = source.m_nearbyWaterKnown;
    m_nearbyWater = source.m_nearbyWater;
    m_waterHeightRange = source.m_waterHeightRange;
    m_valid = source.m_valid;
    m_revision = source.m_revision;
    m_filled = source.m_filled;
    m_packed = source.m_packed;
    m_packedRevision = source.m_packedRevision;
    m_packingEnabled = source.m_packingEnabled;
    m_preparation = source.m_preparation;
}
// Playback can retire a snapshot on another thread. Recycle only after its
// final strong reference ends, using a new control block on reuse: even a weak
// reference to the old revision can never acquire the reused storage.
// Two idle maps cover the normal producer/playback overlap; slow readers keep
// their own maps without a wait, and excess retired storage is freed.
struct GroundMap::SnapshotStorage {
    std::mutex mutex;
    std::unique_ptr<GroundMap> idle[2];
    std::unique_ptr<GroundMap> Take() {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& slot : idle) if (slot) return std::move(slot);
        return nullptr;
    }
    void Return(GroundMap* map) {
        std::unique_ptr<GroundMap> retired(map);
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& slot : idle) if (!slot) { slot = std::move(retired); break; }
    }
};
std::shared_ptr<const GroundMap> GroundMap::Snapshot() const
{
    FinishPreparation();
    if (!m_snapshot || m_snapshot->Revision() != m_revision) {
        if (!m_snapshotStorage) m_snapshotStorage = std::make_shared<SnapshotStorage>();
        // Drop only the owner's reference. Queued readers keep their revision;
        // its deleter returns the map when the last one finishes.
        m_snapshot.reset();
        auto next = m_snapshotStorage->Take();
        if (next) next->CopySnapshotData(*this);
        else next.reset(new GroundMap(*this, SnapshotTag{}));
        m_snapshot = std::shared_ptr<const GroundMap>(next.release(),
            [storage = m_snapshotStorage](GroundMap* map) {
                storage->Return(map);
            });
    }
    return m_snapshot;
}
GroundMap::~GroundMap(){StopPreparation();}
void GroundMap::FinishPreparation() const {
    if(!m_worker || !m_worker->pending)return;
    auto& w=*m_worker;
    if(!w.done.load(std::memory_order_acquire)){
        const auto start=std::chrono::steady_clock::now();
        std::unique_lock<std::mutex> lock(w.mutex);
        w.wake.wait(lock,[&]{return w.done.load(std::memory_order_acquire);});
        ++m_preparation.waited;
        m_preparation.waitMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }else ++m_preparation.ready;
    m_preparation.workerMilliseconds+=w.elapsed;w.pending=false;
    if(w.failure)std::rethrow_exception(w.failure);
}
void GroundMap::StopPreparation(){FinishPreparation();m_worker.reset();}

float GroundMap::OriginFor(float cameraCoordinate)
{
    return std::floor((cameraCoordinate - kArea * 0.5f) / kCellSize) * kCellSize;
}

void GroundMap::Clear()
{
    FinishPreparation();
    std::fill(std::begin(m_height), std::end(m_height), kUnknown);
    std::fill(std::begin(m_liquid), std::end(m_liquid), kUnknown);
    std::fill(std::begin(m_shore), std::end(m_shore), 0.0f);
    std::fill(std::begin(m_land), std::end(m_land), kShoreReach);
    std::fill(std::begin(m_liquidTrust), std::end(m_liquidTrust), 0.0f);
    std::fill(std::begin(m_liquidNear), std::end(m_liquidNear), kUnknown);
    std::memset(m_looked, 0, sizeof(m_looked));
    std::memset(m_hot, 0, sizeof(m_hot));
    m_filled = 0;
    m_nearbyWaterKnown = false; m_waterHeightRange = 0;
    m_ring = 0;
    m_ringStep = 0;
    m_shorePending = true;
    m_shoreBuiltAt = 0.0;
    ++m_revision;
}

void GroundMap::Recentre(const float* origin,bool shiftDerived)
{
    const int dx = static_cast<int>(std::lround((origin[0] - m_origin[0]) / m_cell));
    const int dy = static_cast<int>(std::lround((origin[1] - m_origin[1]) / m_cell));
    water::ShiftGroundCells<kCells>(m_height, dx, dy, kUnknown, false);
    water::ShiftGroundCells<kCells>(m_liquid, dx, dy, kUnknown, false);
    water::ShiftGroundCells<kCells>(m_hot, dx, dy, false, false);
    // What is worked out from the heights moves with them, so it stays in place between rebuilds, and carries on into
    // the cells coming in, which have their heights looked up at once: counted as a shoreline there (0), they showed
    // a flash of foam along the edge of the map each time it moved, and the surface stepped.
    if(shiftDerived)ShiftDerived(dx,dy);
    water::ShiftGroundCells<kCells>(m_looked, dx, dy, false, false);
    m_filled = static_cast<int>(std::count(std::begin(m_looked), std::end(m_looked), true));
    // The cells not yet looked up were in the current ring or further out before the shift, or outside the map (ring
    // kLastRing or further on the high side); after it they are at most the shift nearer the centre, so the rings are
    // gone through again from there.
    const int shift = std::max(std::abs(dx), std::abs(dy));
    m_ring = std::max(0, std::min(m_ring, kLastRing) - shift);
    m_ringStep = 0;
    ++m_revision;
}

void GroundMap::ShiftDerived(int dx,int dy){
    water::ShiftGroundCells<kCells>(m_shore,dx,dy,0.0f,true);
    water::ShiftGroundCells<kCells>(m_land,dx,dy,0.0f,true);
    water::ShiftGroundCells<kCells>(m_liquidTrust,dx,dy,0.0f,true);
    water::ShiftGroundCells<kCells>(m_liquidNear,dx,dy,0.0f,true);
}

// The cells in rings of growing size around the centre, so the ground nearest the camera is known first.
bool GroundMap::NextCell(int& x, int& y)
{
    while (m_ring <= kLastRing)
    {
        const int r = m_ring;
        const int ringCells = r == 0 ? 1 : 8 * r;
        while (m_ringStep < ringCells)
        {
            const int s = m_ringStep++;
            if (r == 0)
            {
                x = kCentre;
                y = kCentre;
            }
            else if (s < 2 * r)
            {
                x = kCentre - r + s;
                y = kCentre - r;
            }
            else if (s < 4 * r)
            {
                x = kCentre + r;
                y = kCentre - r + (s - 2 * r);
            }
            else if (s < 6 * r)
            {
                x = kCentre + r - (s - 4 * r);
                y = kCentre + r;
            }
            else
            {
                x = kCentre - r;
                y = kCentre + r - (s - 6 * r);
            }
            if (x >= 0 && x < kCells && y >= 0 && y < kCells && !m_looked[y * kCells + x])
                return true;
        }
        ++m_ring;
        m_ringStep = 0;
    }
    return false;
}

void GroundMap::Update(const float* camera, GroundHeightFn ground, GroundHeightFn liquid, double budgetSeconds, bool worker, GroundHeightFn hot)
{
    m_preparation={};
    FinishPreparation();
    if(!worker)StopPreparation();
    m_packingEnabled=worker;
    const auto originFor = [&](float coordinate) { return std::floor((coordinate - Area() * 0.5f) / m_cell) * m_cell; };
    const float origin[2] = {originFor(camera[0]), originFor(camera[1])};
    int shiftX=0,shiftY=0;
    if (!m_valid || std::fabs(origin[0] - m_origin[0]) >= Area() || std::fabs(origin[1] - m_origin[1]) >= Area())
    {
        Clear();
        m_valid = true;
    }
    else if (origin[0] != m_origin[0] || origin[1] != m_origin[1])
    {
        shiftX=static_cast<int>(std::lround((origin[0]-m_origin[0])/m_cell));
        shiftY=static_cast<int>(std::lround((origin[1]-m_origin[1])/m_cell));
        Recentre(origin,!worker);
    }
    m_origin[0] = origin[0];
    m_origin[1] = origin[1];

    using Clock = std::chrono::steady_clock;
    // Terrain the game had not loaded yet when asked about, asked about again now and then.
    if (m_retryMissing && m_filled == kCells * kCells)
    {
        const double seconds = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
        if (seconds >= m_retryAt)
        {
            m_retryAt = seconds + kRetryMissingSeconds;
            int missing = 0;
            for (int i = 0; i < kCells * kCells; ++i)
                if (m_height[i] <= kUnknown)
                {
                    m_looked[i] = false;
                    ++missing;
                }
            // (All of it missing: a map with no terrain, not terrain still to come.)
            if (missing == kCells * kCells)
                std::fill(std::begin(m_looked), std::end(m_looked), true);
            else
            {
                m_filled -= missing;
                m_ring = 0;
                m_ringStep = 0;
            }
        }
    }
    const auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                             std::chrono::duration<double>(budgetSeconds));
    bool changed = false;
    int x = 0;
    int y = 0;
    for (int queries = 1; NextCell(x, y); ++queries)
    {
        float height = 0.0f;
        const float wx = m_origin[0] + (x + 0.5f) * m_cell;
        const float wy = m_origin[1] + (y + 0.5f) * m_cell;
        const bool hit = ground(wx, wy, camera[2] + kQueryAbove, camera[2] - kQueryBelow, height);
        m_height[y * kCells + x] = hit && std::isfinite(height) ? height : kUnknown;
        float surface = 0.0f;
        const bool wet = liquid(wx, wy, camera[2] + kQueryAbove, camera[2] - kQueryBelow, surface);
        m_liquid[y * kCells + x] = wet && std::isfinite(surface) ? surface : kUnknown;
        float hotSurface = 0.0f;
        m_hot[y * kCells + x] = wet && (!hot || hot(wx, wy, camera[2] + kQueryAbove, camera[2] - kQueryBelow, hotSurface));
        m_looked[y * kCells + x] = true;
        ++m_filled;
        changed = true;
        // A cell's native intersections must finish together. In a city even
        // a few extra cells can exceed the game thread's remaining budget.
        if (budgetSeconds > 0 ? Clock::now() >= deadline : queries >= kUntimedQueryBatch)
            break;
    }
    if (changed)
    {
        ++m_revision;
        m_shorePending = true;
    }
    // The shore distances take about a millisecond, so while cells keep coming in (the camera travelling, the map
    // filling) they are rebuilt only now and then, and once more as soon as nothing new comes in.
    const double now = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    const bool rebuild=m_shorePending && (!changed || now-m_shoreBuiltAt>=kShoreRebuildSeconds);
    if(rebuild){
        // Final deferred shore rebuilding can happen without any new height
        // queries. It must invalidate the renderer's upload revision too.
        ++m_revision;
        m_shorePending=false;m_shoreBuiltAt=now;
    }
    if(worker && (rebuild || m_packedRevision!=m_revision || m_packed.empty())){
        if(!m_worker){try{m_worker=std::make_unique<Worker>(*this);}catch(const std::system_error&){}}
        if(m_worker){m_worker->Submit(rebuild,shiftX,shiftY);++m_preparation.submitted;
            m_preparation.rebuilds+=rebuild;++m_preparation.packs;
            m_preparation.shifts+=!rebuild && (shiftX||shiftY);
        }else {if(!rebuild && (shiftX||shiftY))ShiftDerived(shiftX,shiftY);
            if(rebuild){BuildShoreDistances();++m_preparation.inlineBuilds;}m_packingEnabled=false;}
    }else if(rebuild){BuildShoreDistances();++m_preparation.inlineBuilds;}
}

namespace
{
constexpr float kFar = 1.0e6f;

// Distances (in cells) from every cell to the nearest source cell (distance 0), by two sweeps with straight and
// diagonal steps. (The edges of each row are taken apart from its middle, so the inner loop has no bounds checks:
// this runs whenever the ground map changes, which is every frame while the camera travels.)
void DistanceTransform(std::vector<float>& distances)
{
    constexpr int kCells = GroundMap::kCells;
    constexpr float kDiagonal = 1.41421356f;
    float* d = distances.data();
    for (int x = 1; x < kCells; ++x)
        d[x] = std::min(d[x], d[x - 1] + 1.0f);
    for (int y = 1; y < kCells; ++y)
    {
        float* row = d + y * kCells;
        const float* up = row - kCells;
        row[0] = std::min({row[0], up[0] + 1.0f, up[1] + kDiagonal});
        for (int x = 1; x < kCells - 1; ++x)
        {
            // What comes from the row before is worked out first: only the step from the left waits on this row.
            const float fromAbove = std::min({row[x], up[x] + 1.0f, up[x - 1] + kDiagonal, up[x + 1] + kDiagonal});
            row[x] = std::min(fromAbove, row[x - 1] + 1.0f);
        }
        const int last = kCells - 1;
        row[last] = std::min({row[last], row[last - 1] + 1.0f, up[last] + 1.0f, up[last - 1] + kDiagonal});
    }
    float* bottom = d + (kCells - 1) * kCells;
    for (int x = kCells - 2; x >= 0; --x)
        bottom[x] = std::min(bottom[x], bottom[x + 1] + 1.0f);
    for (int y = kCells - 2; y >= 0; --y)
    {
        float* row = d + y * kCells;
        const float* down = row + kCells;
        const int last = kCells - 1;
        row[last] = std::min({row[last], down[last] + 1.0f, down[last - 1] + kDiagonal});
        for (int x = kCells - 2; x > 0; --x)
        {
            const float fromBelow = std::min({row[x], down[x] + 1.0f, down[x + 1] + kDiagonal, down[x - 1] + kDiagonal});
            row[x] = std::min(fromBelow, row[x + 1] + 1.0f);
        }
        row[0] = std::min({row[0], row[1] + 1.0f, down[0] + 1.0f, down[1] + kDiagonal});
    }
}

// Each cell's average with its neighbours (those inside the map), across and then down.
void Smooth(std::vector<float>& values, std::vector<float>& scratch)
{
    constexpr int kCells = GroundMap::kCells;
    const float* v = values.data();
    float* across = scratch.data();
    for (int y = 0; y < kCells; ++y)
    {
        const float* row = v + y * kCells;
        float* out = across + y * kCells;
        out[0] = row[0] + row[1];
        for (int x = 1; x < kCells - 1; ++x)
            out[x] = row[x - 1] + row[x] + row[x + 1];
        out[kCells - 1] = row[kCells - 2] + row[kCells - 1];
    }
    float* out = values.data();
    for (int y = 0; y < kCells; ++y)
    {
        const float* above = y > 0 ? across + (y - 1) * kCells : nullptr;
        const float* row = across + y * kCells;
        const float* below = y < kCells - 1 ? across + (y + 1) * kCells : nullptr;
        const float rows = 1.0f + (above ? 1.0f : 0.0f) + (below ? 1.0f : 0.0f);
        for (int x = 0; x < kCells; ++x)
        {
            const float columns = x == 0 || x == kCells - 1 ? 2.0f : 3.0f;
            out[y * kCells + x] = (row[x] + (above ? above[x] : 0.0f) + (below ? below[x] : 0.0f)) / (rows * columns);
        }
    }
}
}

namespace
{
// Carries each known value (a distance of 0) to the cells without one, nearest first, by the same two sweeps as
// DistanceTransform; cells too far from any value keep kFar.
void NearestFill(float* values, std::vector<float>& distances)
{
    constexpr int kCells = GroundMap::kCells;
    constexpr float kDiagonal = 1.41421356f;
    auto take = [&](int i, int x, int y, int dx, int dy, float step) {
        const int nx = x + dx;
        const int ny = y + dy;
        if (nx < 0 || nx >= kCells || ny < 0 || ny >= kCells)
            return;
        const int n = ny * kCells + nx;
        if (distances[n] + step < distances[i])
        {
            distances[i] = distances[n] + step;
            values[i] = values[n];
        }
    };
    for (int y = 0; y < kCells; ++y)
        for (int x = 0; x < kCells; ++x)
        {
            const int i = y * kCells + x;
            take(i, x, y, -1, 0, 1.0f);
            take(i, x, y, 0, -1, 1.0f);
            take(i, x, y, -1, -1, kDiagonal);
            take(i, x, y, 1, -1, kDiagonal);
        }
    for (int y = kCells - 1; y >= 0; --y)
        for (int x = kCells - 1; x >= 0; --x)
        {
            const int i = y * kCells + x;
            take(i, x, y, 1, 0, 1.0f);
            take(i, x, y, 0, 1, 1.0f);
            take(i, x, y, 1, 1, kDiagonal);
            take(i, x, y, -1, 1, kDiagonal);
        }
}
}

// The distance from water to dry ground, smoothed, and how far each cell lies inside the patch where the world gives a
// water height (the game reports it only near the player). Dry ground is ground at or above the water there, or,
// where the world gives no water height, above the nearest water's height (see NearestLiquidHeights).
void GroundMap::BuildShoreDistances()
{
    float waterSum = 0.0f;
    int waterCount = 0;
    float low = 1e9f, high = -1e9f;
    for (int i = 0; i < kCells * kCells; ++i)
        if (m_liquid[i] > kUnknown)
        {
            waterSum += m_liquid[i];
            low = std::min(low, m_liquid[i]); high = std::max(high, m_liquid[i]);
            ++waterCount;
        }
    const bool waterKnown = waterCount > 0;
    const float nearbyWater = waterKnown ? waterSum / waterCount : 0.0f;
    m_nearbyWaterKnown = waterKnown;
    m_nearbyWater = nearbyWater;
    m_waterHeightRange = waterKnown ? high-low : 0;

    const bool partialWater = waterKnown && waterCount < kCells * kCells;
    // Every gap gets the same nearest height on a level surface. Preserve
    // signed zero on mixed-sign inputs by using the ordinary propagation.
    const bool levelSurface = partialWater && low == high &&
        (low != 0 || std::none_of(std::begin(m_liquid), std::end(m_liquid),
            [](float h) { return h == 0 && std::signbit(h); }));
    if (levelSurface) {
        std::fill(std::begin(m_liquidNear), std::end(m_liquidNear), low);
    } else {
        std::copy(std::begin(m_liquid), std::end(m_liquid), m_liquidNear);
        if (partialWater) {
            static thread_local std::vector<float> nearDistance(kCells * kCells);
            for (int i = 0; i < kCells * kCells; ++i)
                nearDistance[i] = m_liquid[i] > kUnknown ? 0.0f : kFar;
            NearestFill(m_liquidNear, nearDistance);
        }
    }

    static thread_local std::vector<float> d(kCells * kCells);
    static thread_local std::vector<float> inside(kCells * kCells);
    static thread_local std::vector<float> land(kCells * kCells);
    int dryCells = 0;
    bool hasHotShore = false;
    for (int i = 0; i < kCells * kCells; ++i)
    {
        const bool groundKnown = m_height[i] > kUnknown;
        const bool liquidKnown = m_liquid[i] > kUnknown;
        // Where the world gives no water height, ground is dry if it stands above the nearest water's.
        const bool dry = groundKnown && (liquidKnown ? m_height[i] >= m_liquid[i]
                                                     : (!waterKnown || m_height[i] > m_liquidNear[i] + kDryAboveWater));
        dryCells += dry;
        d[i] = dry ? 0.0f : kFar;
        inside[i] = liquidKnown ? kFar : 0.0f;
        // (Where the ground is not known it is no water for the land beside it to be near.)
        land[i] = groundKnown && !dry && m_hot[i] ? 0.0f : kFar;
        hasHotShore |= land[i] == 0.0f;
    }
    if (partialWater) DistanceTransform(inside);
    for (int i = 0; i < kCells * kCells; ++i)
        m_liquidTrust[i] = m_liquid[i] > kUnknown ? std::min(inside[i] * m_cell / kLiquidTrustReach, 1.0f) : 0.0f;

    static thread_local std::vector<float> scratch(kCells * kCells);
    if (dryCells == kCells * kCells || (dryCells == 0 && kFar * m_cell >= kShoreReach)) {
        // The transform and both smoothing passes preserve these constants.
        const float distance = dryCells ? 0.0f : kShoreReach;
        std::fill(std::begin(m_shore), std::end(m_shore), distance);
    } else {
        DistanceTransform(d);
        for (float& distance : d) distance = std::min(distance * m_cell, kShoreReach);
        for (int pass = 0; pass < 2; ++pass) Smooth(d, scratch);
        std::copy(d.begin(), d.end(), m_shore);
    }

    // With no hot shore seed the distance field saturates everywhere.
    // Smoothing this constant (12 yards) also gives 12, including at grid edges.
    if (!hasHotShore && kFar * m_cell >= kShoreReach)
    {
        std::fill(std::begin(m_land), std::end(m_land), kShoreReach);
        return;
    }
    static thread_local std::vector<float> wetSide(kCells * kCells);
    std::fill(wetSide.begin(), wetSide.end(), -m_cell);
    // Where the shore falls between a dry cell and the liquid next to it, by their heights over that liquid: a cell's whole
    // width from one to the next made steps in the distance, at a coarse map's cell size scallops along a slanting shore.
    for (int y = 0; y < kCells; ++y)
        for (int x = 0; x < kCells; ++x)
        {
            const int i = y * kCells + x;
            if (land[i] == 0.0f || m_height[i] <= kUnknown)
                continue;
            const int neighbours[4] = {x > 0 ? i - 1 : -1, x < kCells - 1 ? i + 1 : -1, y > 0 ? i - kCells : -1, y < kCells - 1 ? i + kCells : -1};
            for (int n : neighbours)
                if (n >= 0 && land[n] == 0.0f && m_liquid[n] > kUnknown)
                {
                    const float above = std::max(m_height[i] - m_liquid[n], 0.0f), under = std::max(m_liquid[n] - m_height[n], 0.0f);
                    land[i] = std::min(land[i], above + under > 1.0e-4f ? above / (above + under) : 1.0f);
                }
        }
    DistanceTransform(land);
    for (float& value : land) value = std::min(value * m_cell, kShoreReach);
    // Inside the liquid the distance runs on below zero, so that read between cells it crosses zero at the shore itself, not at a
    // liquid cell's middle: that moved with how the shore fell on the cells, in scallops.
    for (int y = 0; y < kCells; ++y)
        for (int x = 0; x < kCells; ++x)
        {
            const int i = y * kCells + x;
            if (land[i] != 0.0f || m_liquid[i] <= kUnknown)
                continue;
            float inward = 1.0f;
            const int neighbours[4] = {x > 0 ? i - 1 : -1, x < kCells - 1 ? i + 1 : -1, y > 0 ? i - kCells : -1, y < kCells - 1 ? i + kCells : -1};
            for (int n : neighbours)
                if (n >= 0 && land[n] > 0.0f && m_height[n] > kUnknown)
                {
                    const float above = std::max(m_height[n] - m_liquid[i], 0.0f), under = std::max(m_liquid[i] - m_height[i], 0.0f);
                    inward = std::min(inward, above + under > 1.0e-4f ? under / (above + under) : 1.0f);
                }
            wetSide[i] = -inward * m_cell;
        }
    for (int i = 0; i < kCells * kCells; ++i)
        if (land[i] == 0.0f)
            land[i] = wetSide[i];
    for (int pass = 0; pass < 2; ++pass) Smooth(land, scratch);
    std::copy(land.begin(), land.end(), m_land);
}

void GroundMap::PackTexture(){
    m_packed.resize(size_t(kCells)*kCells*4);
    const float fallback=m_nearbyWaterKnown?m_nearbyWater:0;
    for(int i=0;i<kCells*kCells;++i){
        const float h=m_height[i],l=m_liquidNear[i];
        m_packed[size_t(i)*4]=h>kUnknown?h:0;
        m_packed[size_t(i)*4+1]=h>kUnknown?1.f:0;
        m_packed[size_t(i)*4+2]=l>kUnknown?l:fallback;
        m_packed[size_t(i)*4+3]=m_liquidTrust[i];
    }
    m_packedRevision=m_revision;
}

float GroundMap::WaterHeightAt(float x, float y) const
{
    FinishPreparation();
    if (!m_valid || !m_nearbyWaterKnown || !std::isfinite(x) || !std::isfinite(y)) return m_nearbyWater;
    const float gx=std::clamp((x-m_origin[0])/m_cell-.5f,0.0f,float(kCells-1));
    const float gy=std::clamp((y-m_origin[1])/m_cell-.5f,0.0f,float(kCells-1));
    const int ix=int(gx), iy=int(gy), nx=std::min(ix+1,kCells-1), ny=std::min(iy+1,kCells-1);
    const float fx=gx-ix, fy=gy-iy;
    const int cells[]={iy*kCells+ix,iy*kCells+nx,ny*kCells+ix,ny*kCells+nx};
    const float weights[]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
    float height=0, weight=0;
    for(int i=0;i<4;++i)if(m_liquidNear[cells[i]]>kUnknown){height+=weights[i]*m_liquidNear[cells[i]];weight+=weights[i];}
    return weight>0 ? height/weight : m_nearbyWater;
}

float GroundMap::ReflectionHeight(const float* object, const float* camera) const
{
    // Locate the water crossed by the eye-to-mirrored-centre ray. A bounded
    // fixed-point solve handles gentle river slopes without rotating geometry.
    float height=WaterHeightAt(object[0],object[1]);
    for(int i=0;i<4;++i){
        const float denominator=2*height-object[2]-camera[2];
        const float t=std::fabs(denominator)>1e-4f ? std::clamp((height-camera[2])/denominator,0.0f,1.0f):.5f;
        height=WaterHeightAt(camera[0]+t*(object[0]-camera[0]),camera[1]+t*(object[1]-camera[1]));
    }
    return height;
}
