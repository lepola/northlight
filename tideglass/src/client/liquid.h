#pragma once
// Tells which kind of liquid a draw is. The client's own liquid declarations decide wherever they list the texture;
// otherwise the texture's average colour does.

#include <d3d9.h>

#include <unordered_map>
#include "client/native_liquid.h"

const char* LiquidKindName(LiquidKind kind);
LiquidKind ClassifyLiquidColor(float red, float green, float blue);
bool AverageTextureColor(IDirect3DBaseTexture9* texture, float* rgb, D3DSURFACE_DESC& desc);

class LiquidClassifier
{
public:
    ~LiquidClassifier() { ReleaseAll(); }

    LiquidKind Classify(IDirect3DBaseTexture9* texture);
    // The native declarations decide every texture they list (conflicting ones stay unknown). A texture they do not list is
    // judged by its colour, as without native data: water in buildings and dungeons (Blackfathom Deeps) is drawn with
    // textures outside the client's liquid material table, and was left as the client's own water (2026-11-09). The
    // generated ramps whose colours change under water are listed, so they never reach the colour judgement.
    // bounds: the box around the draw's vertices (LiquidDrawBounds), if known. The ocean and river ramps are then judged
    // over the surface's own extent, not where the camera is.
    LiquidKind Classify(IDirect3DBaseTexture9* texture,const NativeLiquidFrame& frame,const float* bounds=nullptr,int* style=nullptr)
    {
        const auto identity=reinterpret_cast<uintptr_t>(texture);
        const int ramp=frame.Ramp(identity);
        if(style)*style=ramp;
        if(!frame.known || !frame.Lists(identity))return Classify(texture);
        return bounds && ramp>=0 ? frame.StyleAt(bounds,unsigned(ramp)) : frame.Find(identity,style);
    }
    void ReleaseAll();

private:
    struct Verdict
    {
        LiquidKind kind;
        unsigned long checkedAt;
    };
    // The client recycles texture objects for new images of the same size (a Felwood slime ramp can become an
    // Elwynn water ramp), so a verdict is only trusted for a short while before the texture is looked at again.
    std::unordered_map<IDirect3DBaseTexture9*, Verdict> m_kinds;
};
