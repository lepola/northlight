#include "world_mesh_page_gpu.h"
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace NorthlightWorldMeshPages;
struct FakeBudget {
    size_t remaining=CopyChunkBytes;
    bool timeAvailable=true;
    mutable size_t checks=0;
    bool available()const{++checks;return remaining&&timeAvailable;}
    size_t chunk(size_t desired)const{return std::min(desired,remaining);}
    void consume(size_t n){assert(n<=remaining);remaining-=n;}
};
struct FakePage {
    std::vector<uint8_t> sourceV,sourceI,gpuV,gpuI;
    bool preloadV=false,preloadI=false;
};
struct FakeBackend {
    std::vector<FakePage> pages;
    size_t calls=0,copies=0,failAt=0;
    explicit FakeBackend(const std::vector<PageBytes>& sizes){
        for(size_t i=0;i<sizes.size();++i){
            FakePage p;p.sourceV.resize(sizes[i].vertexBytes);p.sourceI.resize(sizes[i].indexBytes);
            for(size_t j=0;j<p.sourceV.size();++j)p.sourceV[j]=uint8_t(j*7+i*19);
            for(size_t j=0;j<p.sourceI.size();++j)p.sourceI[j]=uint8_t(j*13+i*23);
            pages.push_back(std::move(p));
        }
    }
    bool operator()(Step step,size_t page,size_t offset,size_t bytes){
        ++calls;if(calls==failAt)return false;
        assert(page<pages.size()&&bytes&&bytes<=MaxBufferBytes);
        auto& p=pages[page];
        switch(step){
        case Step::CreateVertices:assert(!offset);p.gpuV.assign(bytes,0);break;
        case Step::CreateIndices:assert(!offset);p.gpuI.assign(bytes,0);break;
        case Step::CopyVertices:
            assert(bytes<=CopyChunkBytes&&offset+bytes<=p.gpuV.size());
            std::copy_n(p.sourceV.begin()+offset,bytes,p.gpuV.begin()+offset);++copies;break;
        case Step::CopyIndices:
            assert(p.preloadV&&bytes<=CopyChunkBytes&&offset+bytes<=p.gpuI.size());
            std::copy_n(p.sourceI.begin()+offset,bytes,p.gpuI.begin()+offset);++copies;break;
        case Step::PreloadVertices:
            assert(!offset&&bytes==p.gpuV.size()&&p.gpuV==p.sourceV);p.preloadV=true;break;
        case Step::PreloadIndices:
            assert(!offset&&bytes==p.gpuI.size()&&p.gpuI==p.sourceI);p.preloadI=true;break;
        }
        return true;
    }
};
static void finish(Upload& upload,FakeBackend& backend){
    size_t calls=0;
    while(upload.status()==Result::Pending){
        assert(++calls<10000);
        // Deliberately cross chunk boundaries and exhaust each byte allowance.
        FakeBudget budget;budget.remaining=17003;
        const auto before=backend.calls;
        const auto state=upload.advance(backend,budget);
        assert(backend.calls==before+1&&budget.checks==1&&state!=Result::Failed);
    }
    assert(upload.status()==Result::Ready);
    for(const auto& p:backend.pages)assert(p.preloadV&&p.preloadI&&p.sourceV==p.gpuV&&p.sourceI==p.gpuI);
}
int main(){
    const std::vector<PageBytes> sizes={{MaxBufferBytes,MaxBufferBytes},{91,17},{CopyChunkBytes+11,CopyChunkBytes-1}};
    Upload upload;FakeBackend backend(sizes);FakeBudget budget;
    assert(upload.advance(backend,budget)==Result::Pending&&backend.calls==0);
    assert(upload.begin(sizes));
    budget.timeAvailable=false;
    assert(upload.advance(backend,budget)==Result::Pending&&backend.calls==0);
    budget.timeAvailable=true;budget.remaining=0;
    assert(upload.advance(backend,budget)==Result::Pending&&backend.calls==0);
    budget.remaining=CopyChunkBytes;
    assert(upload.advance(backend,budget)==Result::Pending&&backend.calls==1);
    assert(upload.step()==Step::CreateIndices&&budget.remaining==CopyChunkBytes);
    // A costly create exhausts time: no next driver call until a fresh budget.
    budget.timeAvailable=false;
    assert(upload.advance(backend,budget)==Result::Pending&&backend.calls==1);
    finish(upload,backend);
    const auto completeCalls=backend.calls;
    budget.timeAvailable=true;
    assert(upload.advance(backend,budget)==Result::Ready&&backend.calls==completeCalls);

    // Reset cancels midway through a copy and discards cursor offsets. A new
    // upload starts from CreateVertices, even when reusing the same slot.
    assert(upload.begin(sizes));FakeBackend cancelled(sizes);
    for(size_t i=0;i<3;++i){FakeBudget b;upload.advance(cancelled,b);}
    assert(upload.step()==Step::CopyVertices&&upload.offset()==CopyChunkBytes);
    upload.reset();
    assert(upload.offset()==0&&upload.pageIndex()==0&&upload.step()==Step::CreateVertices);
    const auto cancelledCalls=cancelled.calls;
    assert(upload.advance(cancelled,budget)==Result::Pending&&cancelled.calls==cancelledCalls);
    assert(upload.begin(sizes));FakeBackend replacement(sizes);finish(upload,replacement);

    // Fail each operation, including the final preload. The renderer's old
    // committed generation remains active; publication is possible only Ready.
    Upload reference;assert(reference.begin(sizes));FakeBackend refBackend(sizes);finish(reference,refBackend);
    for(size_t failure=1;failure<=refBackend.calls;++failure){
        assert(upload.begin(sizes));FakeBackend failing(sizes);failing.failAt=failure;
        int activeGeneration=7;
        for(size_t i=0;i<failure;++i){
            FakeBudget b;b.remaining=17003;
            if(upload.advance(failing,b)==Result::Ready)activeGeneration=8;
        }
        assert(upload.status()==Result::Failed&&activeGeneration==7&&failing.calls==failure);
        assert(upload.advance(failing,budget)==Result::Failed&&failing.calls==failure);
    }
    {const std::vector<PageBytes> aligned={{CopyChunkBytes+32,400,32}};Upload u;assert(u.begin(aligned));FakeBackend b(aligned);
     while(u.status()==Result::Pending){FakeBudget allowance;allowance.remaining=17003;const auto step=u.step();u.advance([&](Step current,size_t page,size_t at,size_t bytes){if(current==Step::CopyVertices)assert(at%32==0&&bytes%32==0);return b(current,page,at,bytes);},allowance);if(step==Step::CopyVertices)assert(u.offset()%32==0||u.step()!=Step::CopyVertices);}
     assert(u.status()==Result::Ready&&b.pages[0].sourceV==b.pages[0].gpuV);assert(!u.begin({{33,2,32}}));}
    assert(!upload.begin({{MaxBufferBytes+1,1}}));
    assert(!upload.begin({{1,MaxBufferBytes+1}}));
    assert(!upload.begin({{0,1}}));assert(!upload.begin({{1,0}}));
    assert(upload.begin({})&&upload.status()==Result::Ready);
    std::cout<<"Paged mesh upload budget, byte fidelity, cancellation, and failure tests passed\n";
}
