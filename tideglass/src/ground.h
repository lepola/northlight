#pragma once
#include <memory>
#include <vector>
#include <cstdint>

// The ground height and the liquid surface height over a square of the world around the camera, looked up from the
// game's own world data (not the screen), so they are exact from any view: the ground ignores rocks, buildings and
// water, and the liquid height ignores how the surface happens to be drawn. It is filled nearest first
// within a time budget each frame and shifts with the camera, so once filled only new strips are looked up.
using GroundHeightFn = bool (*)(float x, float y, float fromZ, float toZ, float& height);

class GroundMap
{
public:
    static constexpr int kCells = 256;
    static constexpr float kCellSize = 0.6f;
    static constexpr float kArea = kCells * kCellSize;

    GroundMap();
    // A coarser map of the same size, reaching further. retryMissing: ground not yet loaded when asked about is asked about again.
    GroundMap(float cellSize, bool retryMissing);
    ~GroundMap();
    float CellSize() const { return m_cell; }
    float Area() const { return kCells * m_cell; }
    GroundMap(const GroundMap&)=delete;
    GroundMap& operator=(const GroundMap&)=delete;
    struct PreparationStats {
        unsigned submitted=0,ready=0,waited=0,inlineBuilds=0,rebuilds=0,packs=0,shifts=0;
        double workerMilliseconds=0,waitMilliseconds=0;
    };
    const PreparationStats& Preparation() const { return m_preparation; }
    // Resolve the exact current rebuild. Update/Clear/destruction also resolve it
    // before touching its numeric inputs; no previous-frame derived map is used.
    void FinishPreparation() const;
    // Called by the map's owner after updates, before publishing a frame. Reuses
    // the last revision and never shares mutable worker storage with playback.
    std::shared_ptr<const GroundMap> Snapshot() const;
    uint64_t Identity() const { return m_identity; }

    static float OriginFor(float cameraCoordinate);

    // hot: where the liquid is what land beside it is measured from (null: any liquid).
    void Update(const float* camera, GroundHeightFn ground, GroundHeightFn liquid, double budgetSeconds, bool worker=false, GroundHeightFn hot=nullptr);
    void Clear();

    bool Valid() const { return m_valid; }
    const float* Origin() const { return m_origin; }
    // Per cell: the ground height, or kUnknown where it has not been looked up or there is no terrain.
    const float* Heights() const { return m_height; }
    // Per cell: the liquid surface height, or kUnknown where there is none.
    const float* LiquidHeights() const { return m_liquid; }
    // Per cell: the liquid surface height there or, where there is none, at the nearest cell that has one (so water
    // beyond where the world reports its height carries on at the height of the water nearest it); kUnknown where no
    // liquid height is known anywhere on the map. Rebuilt with the shore distances.
    const float* NearestLiquidHeights() const { FinishPreparation(); return m_liquidNear; }
    // Per cell: how far (yards) water is from the nearest dry ground, smoothed and capped at kShoreReach; 0 on dry
    // ground. Rebuilt a few times a second while the heights change, and once they stop.
    const float* ShoreDistances() const { FinishPreparation(); return m_shore; }
    static constexpr float kShoreReach = 12.0f;
    // Per cell: how far (yards) dry ground is from the nearest hot liquid (see Update), smoothed and capped at kShoreReach; below
    // 0 in it. What lies beside a lava's shore, in the world, wherever the camera looks from. Where the shore falls between two cells is
    // worked out from their heights, so the distance has no steps of a cell's size.
    const float* LandDistances() const { FinishPreparation(); return m_land; }
    // Per cell: how much the world's water height is trusted, 0 at the edge of the patch where the world gives one,
    // rising to 1 over kLiquidTrustReach inside it (so the surface does not switch height at the patch's edge).
    const float* LiquidTrust() const { FinishPreparation(); return m_liquidTrust; }
    static constexpr float kLiquidTrustReach = 5.0f;
    // The world's water height around the player (the mean of the known cells), if any is known: a level that does
    // not depend on where the camera looks.
    bool NearbyWaterKnown() const { FinishPreparation(); return m_nearbyWaterKnown; }
    float NearbyWater() const { FinishPreparation(); return m_nearbyWater; }
    float WaterHeightRange() const { FinishPreparation(); return m_waterHeightRange; }
    // Bilinear nearest-water height, clamped to the camera map at its edges.
    float WaterHeightAt(float x, float y) const;
    float ReflectionHeight(const float* object, const float* camera) const;
    static constexpr float kDryAboveWater = 0.3f;
    // CPU RGBA upload staging, owned by this map; GPU locks/uploads stay on the
    // renderer thread. Null selects the original inline packing path.
    const float* PackedTexture() const {FinishPreparation();return m_packingEnabled && m_packedRevision==m_revision && !m_packed.empty()?m_packed.data():nullptr;}
    unsigned Revision() const { return m_revision; }
    int Filled() const { return m_filled; }

    static constexpr float kUnknown = -1.0e9f;

private:
    struct SnapshotTag {};
    GroundMap(const GroundMap& source, SnapshotTag);
    uint64_t m_identity;
    mutable std::shared_ptr<const GroundMap> m_snapshot;
    struct SnapshotStorage;
    mutable std::shared_ptr<SnapshotStorage> m_snapshotStorage;
    void CopySnapshotData(const GroundMap& source);
    void Recentre(const float* origin, bool shiftDerived);
    void ShiftDerived(int dx,int dy);
    bool NextCell(int& x, int& y);
    void BuildShoreDistances();
    void PackTexture();
    std::vector<float> m_packed;
    unsigned m_packedRevision=0;
    bool m_packingEnabled=false;
    void StopPreparation();
    struct Worker;
    mutable std::unique_ptr<Worker> m_worker;
    mutable PreparationStats m_preparation;


    float m_height[kCells * kCells];
    float m_liquid[kCells * kCells];
    float m_shore[kCells * kCells];
    float m_land[kCells * kCells];
    bool m_hot[kCells * kCells];
    float m_cell = kCellSize;
    bool m_retryMissing = false;
    double m_retryAt = 0.0;
    float m_liquidTrust[kCells * kCells];
    float m_liquidNear[kCells * kCells];
    bool m_nearbyWaterKnown = false;
    float m_nearbyWater = 0.0f;
    float m_waterHeightRange = 0.0f;
    bool m_looked[kCells * kCells];
    float m_origin[2] = {};
    bool m_valid = false;
    unsigned m_revision = 0;
    bool m_shorePending = true;
    double m_shoreBuiltAt = 0.0;
    int m_filled = 0;
    int m_ring = 0;
    int m_ringStep = 0;
};
