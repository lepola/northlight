namespace NorthlightTerrainCapture {
template<bool Validated=false,class VertexAt>
inline bool referenceTriangleSequence(VertexAt vertexAt,std::size_t count,
                                     D3DPRIMITIVETYPE topology,Bounds& output) {
    output=Bounds{};
    if(count<3||(topology!=D3DPT_TRIANGLELIST&&topology!=D3DPT_TRIANGLESTRIP)||
       (topology==D3DPT_TRIANGLELIST&&count%3))return false;
    constexpr double origin=17066.666666666666,step=100.0/3.0;
    constexpr double tolerance=.03;
    const float infinity=std::numeric_limits<float>::infinity();
    Bounds result;for(unsigned j=0;j<3;++j){result.low[j]=infinity;result.high[j]=-infinity;}
    std::unordered_set<unsigned> seen;
    unsigned previousChunk=std::numeric_limits<unsigned>::max();
    const std::size_t triangles=topology==D3DPT_TRIANGLELIST?count/3:count-2;
    for(std::size_t triangle=0;triangle<triangles;++triangle){
        const std::size_t first=topology==D3DPT_TRIANGLELIST?triangle*3:triangle;
        const Position p[]={vertexAt(first),vertexAt(first+1),vertexAt(first+2)};
        if constexpr(!Validated)
            for(unsigned i=0;i<3;++i)if(!std::isfinite(p[i].x)||!std::isfinite(p[i].y)||!std::isfinite(p[i].z))return false;
        const double area=(double(p[1].x)-p[0].x)*(double(p[2].y)-p[0].y)-
                          (double(p[1].y)-p[0].y)*(double(p[2].x)-p[0].x);
        if(std::fabs(area)<1e-9)continue; // Includes strip connectors.
        const double centerX=(double(p[0].x)+p[1].x+p[2].x)/3.;
        const double centerY=(double(p[0].y)+p[1].y+p[2].y)/3.;
        const double gridX=std::floor((origin-centerY)/step);
        const double gridY=std::floor((origin-centerX)/step);
        if(gridX<0||gridX>=1024||gridY<0||gridY>=1024)return false;
        const int x=int(gridX),y=int(gridY);
        const double minX=origin-(y+1)*step,maxX=origin-y*step;
        const double minY=origin-(x+1)*step,maxY=origin-x*step;
        for(unsigned i=0;i<3;++i){
            if(p[i].x<minX-tolerance||p[i].x>maxX+tolerance||
               p[i].y<minY-tolerance||p[i].y>maxY+tolerance)return false;
            const float values[]={p[i].x,p[i].y,p[i].z};
            for(unsigned j=0;j<3;++j){result.low[j]=std::min(result.low[j],values[j]);result.high[j]=std::max(result.high[j],values[j]);}
        }
        const unsigned chunk=unsigned(y)*1024+unsigned(x);
        // Chunk-local triangles are contiguous in normal terrain draws. Avoid
        // a hash lookup for every triangle without changing disjoint coverage.
        if(chunk!=previousChunk&&seen.insert(chunk).second)
            result.chunks.push_back({x,y,float(minX),float(minY),float(maxX),float(maxY)});
        previousChunk=chunk;
    }
    if(result.chunks.empty())return false;
    std::sort(result.chunks.begin(),result.chunks.end(),[](const ChunkBounds&a,const ChunkBounds&b){return a.y<b.y||(a.y==b.y&&a.x<b.x);});
    output=std::move(result);return true;
}

}
