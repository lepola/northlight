#pragma once
#include <d3d9.h>
#include <array>
struct FrameInputs;
namespace water
{
struct SceneryImage
{
    IDirect3DTexture9* colour=nullptr;
    IDirect3DTexture9* depth=nullptr;
    const float* projection=nullptr;
    std::array<float,32> planes{};
    float height=0;
    float receiverHeight=0; // for matching flat authored receivers to native liquid depth
    std::array<float,2> continuation={1,0}; // native-height fallback outside imported quads; empty when low >= high
    std::array<float,4> coverageEdges={}; // outer world-space rectangle, min XY then max XY
    float coverageWidth=0;
    bool heights=false,underwater=false;
};
// Borrowed for the current frame only. The producer owns geometry and its one
// reusable colour/depth pair. The liquid renderer resolves each view immediately
// into its existing reflection buffers before asking for the next image.
class SceneryViews
{
public:
    virtual ~SceneryViews()=default;
    virtual unsigned ViewCount()const=0;
    virtual bool ViewArea(const FrameInputs&,unsigned,RECT&)const=0;
    virtual IDirect3DTexture9* ReceiverMask()const=0;
    virtual bool DrawView(IDirect3DDevice9*,const FrameInputs&,unsigned,SceneryImage&)=0;
};
}
