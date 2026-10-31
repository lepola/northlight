#!/usr/bin/env python3
# northlight-test: requires=cxx,client
"""Run the actual pure chunk classifier natively; compile D3D API side separately.

The native harness extracts the classifier/struct declarations from
the production header; it does not substitute a second Python implementation.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib,json,subprocess,tempfile
from pathlib import Path

HERE=Path(__file__).resolve().parent
header=fp.src('terrain_capture_bounds.h').read_text()
structs=header[header.index('struct ChunkBounds'):header.index('struct MeshSnapshot')]
classifier_start=header.rfind('template<',0,header.index('inline bool classifyTriangleSequence('))
assert classifier_start>=0
classifier=header[classifier_start:header.index('class FrameCache')]
source=r'''
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>
#include <cassert>
#include <cstdio>
enum D3DPRIMITIVETYPE {D3DPT_TRIANGLELIST=4,D3DPT_TRIANGLESTRIP=5};
'''+structs+classifier+r'''
static std::vector<Position> cell(int x,int y){
    double origin=17066.666666666666,step=100./3.;
    float hiX=float(origin-y*step),hiY=float(origin-x*step);
    float loX=float(origin-(y+1)*step),loY=float(origin-(x+1)*step);
    return {{loX,loY,3},{hiX,loY,4},{loX,hiY,5}};
}
int main(int argc,char**argv){
    Bounds result;
    auto one=cell(547,820);
    assert(classifyTriangles(one.data(),one.size(),D3DPT_TRIANGLELIST,result));
    assert(result.chunks.size()==1&&result.chunks[0].x==547&&result.chunks[0].y==820);
    auto far=cell(550,823);auto both=one;both.insert(both.end(),far.begin(),far.end());
    assert(classifyTriangles(both.data(),both.size(),D3DPT_TRIANGLELIST,result));
    assert(result.chunks.size()==2); // Never fill the 4x4 enclosing rectangle.
    auto crossing=one;crossing[2]=far[2];
    assert(!classifyTriangles(crossing.data(),crossing.size(),D3DPT_TRIANGLELIST,result));
    assert(result.chunks.empty());
    auto nonfinite=one;nonfinite[1].z=std::numeric_limits<float>::quiet_NaN();
    assert(!classifyTriangles(nonfinite.data(),nonfinite.size(),D3DPT_TRIANGLELIST,result));
    std::vector<Position> strip={one[0],one[1],one[2],one[2],far[0],far[0],far[1],far[2]};
    assert(classifyTriangles(strip.data(),strip.size(),D3DPT_TRIANGLESTRIP,result));
    assert(result.chunks.size()==2); // Degenerate connectors must not mask gaps.
    std::vector<std::uint32_t> order={0,1,2,3,4,5,6,7};
    Bounds indexed;
    assert(classifyIndexedTriangles(strip.data(),strip.size(),order.data(),order.size(),D3DPT_TRIANGLESTRIP,indexed));
    assert(indexed.chunks.size()==2);
    order[4]=unsigned(strip.size());
    assert(!classifyIndexedTriangles(strip.data(),strip.size(),order.data(),order.size(),D3DPT_TRIANGLESTRIP,indexed));
    assert(indexed.chunks.empty());
    assert(!classifyIndexedTriangles(nullptr,0,order.data(),order.size(),D3DPT_TRIANGLESTRIP,indexed));
    assert(!classifyIndexedTriangles(strip.data(),strip.size(),nullptr,order.size(),D3DPT_TRIANGLESTRIP,indexed));
    assert(argc==2);FILE* f=std::fopen(argv[1],"rb");assert(f);
    unsigned header[5];assert(std::fread(header,4,5,f)==5);
    assert(header[0]==0x32534746&&header[1]==2);
    std::vector<float> vertices(std::size_t(header[2])*8);assert(std::fread(vertices.data(),4,vertices.size(),f)==vertices.size());
    std::vector<unsigned> indices(std::size_t(header[3])*4);assert(std::fread(indices.data(),4,indices.size(),f)==indices.size());std::fclose(f);
    std::vector<Position> triangleVertices;triangleVertices.reserve(std::size_t(header[3])*3);
    for(std::size_t t=0;t<header[3];++t)for(unsigned k=0;k<3;++k){unsigned v=indices[t*4+k];assert(v<header[2]);triangleVertices.push_back({vertices[v*8],vertices[v*8+1],vertices[v*8+2]});}
    assert(classifyTriangles(triangleVertices.data(),triangleVertices.size(),D3DPT_TRIANGLELIST,result));
    assert(result.chunks.size()==256);
    for(const auto& c:result.chunks)assert(c.x>=32*16&&c.x<33*16&&c.y>=50*16&&c.y<51*16);
    std::puts("Passed: global grid, disjoint draws, crossing rejection, nonfinite rejection, strip connectors, all 256 actual ADT chunks.");
}
'''

terrain_key=hashlib.sha256(b'FGS3-v1:terrain:Azeroth:32:50').hexdigest()
model=fp.client_root()/'world-cache/models'/(terrain_key+'.fgs')
with tempfile.TemporaryDirectory(prefix='northlight-terrain-test-') as directory:
    directory=Path(directory);cpp=directory/'test.cpp';binary=directory/'test'
    cpp.write_text(source)
    subprocess.run(['clang++','-std=c++17','-O2','-Wall','-Wextra',str(cpp),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary),str(model)],check=True,capture_output=True,text=True)
    print(result.stdout,end='')
report={'source_sha256':hashlib.sha256(header.encode()).hexdigest(),'native_coverage_tests_passed':True,
        'real_ADT_chunk_count':256,'cross_compile_target':'x86-windows-gnu','game_launched':False}
(fp.output_dir()/'terrain-capture-bounds-validation.json').write_text(json.dumps(report,indent=2)+'\n')
