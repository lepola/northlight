// Where a screen pixel lies in the independently rendered mirrored view, and back (cScenery* constants).
float2 SceneryRaster(float2 pixel)
{
    float2 uv=(pixel-cViewportPx.xy)/cViewportPx.zw;
    if(cSceneryAlignment.x<.5)uv=(pixel+.5-cViewportPx.xy)/cViewportPx.zw;
    float2 bias=float2(1-cSceneryMapping.x+cSceneryMapping.z,1-cSceneryMapping.y-cSceneryMapping.w)*.5;
    float2 at=(uv*cSceneryMapping.xy+bias)*cSceneryGrid.xy;
    if(cSceneryAlignment.x<.5)at-=.5;
    return at;
}
float2 SceneryPixel(float2 at)
{
    if(cSceneryAlignment.x<.5)at+=.5;
    float2 uv=at*cSceneryGrid.zw;
    float2 bias=float2(1-cSceneryMapping.x+cSceneryMapping.z,1-cSceneryMapping.y-cSceneryMapping.w)*.5;
    float2 pixel=cViewportPx.xy+(uv-bias)/cSceneryMapping.xy*cViewportPx.zw;
    if(cSceneryAlignment.x<.5)pixel-=.5;
    return pixel;
}
