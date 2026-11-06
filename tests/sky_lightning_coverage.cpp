#define main capture_fixture_main
#include "test_particle_light_capture.cpp"
#undef main
struct SkyVertex {float position[3];D3DCOLOR colour;float uv[2],uv1[2];};
int main(int argc,char** argv){
    assert(argc==3);std::setvbuf(stdout,nullptr,_IONBF,0);FILE* f=std::fopen(argv[1],"rb");assert(f);
    Device device;device.declaration.elements.insert(device.declaration.elements.end()-1,{0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1});
    device.constants[8*4]=device.constants[9*4+1]=1;
    Shader model[2];Pixel pixels[4];NorthlightParticleCapture::Capture capture;
    for(auto& vs:model){vs.code=words(f);assert(family(vs)==3);capture.registerVertex(&vs,3);}
    for(auto& ps:pixels){const auto code=words(f);NorthlightParticleLightProof::Pixel proof;
        assert(NorthlightParticleLightProof::compilePixel(reinterpret_cast<const std::uint32_t*>(code.data()),code.size(),proof));capture.registerPixel(&ps,code.data(),code.size());}
    assert(capture.catalog.read(argv[2]));NorthlightParticleTextures::enabled=true;
    const float inverse[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},camera[3]{},projection[3]={1,1,1};
    unsigned cases=0;assert(std::fread(&cases,4,1,f)==1&&cases>4);unsigned timelines=0,masked=0;
    for(unsigned packet=0;packet<cases;++packet){
        unsigned textures=0;assert(std::fread(&textures,4,1,f)==1&&textures&&textures<=2);masked+=textures==2;
        unsigned maskImage[5]{};std::vector<unsigned char> maskBytes;
        for(unsigned slot=0;slot<textures;++slot){unsigned image[5];assert(std::fread(image,4,5,f)==5);std::vector<unsigned char> raw(image[4]);assert(std::fread(raw.data(),1,raw.size(),f)==raw.size());
            auto& tex=slot?device.secondTexture:device.texture;
            NorthlightParticleTextures::cache().upload(&tex,&tex,image[0],image[1],0,image[2],raw.data(),raw.size(),image[3],true,true);
            if(slot){std::copy_n(image,5,maskImage);maskBytes=raw;}
        }
        unsigned count=0;assert(std::fread(&count,4,1,f)==1&&count&&count%3==0);std::vector<SkyVertex> vertices(count);assert(std::fread(vertices.data(),sizeof(SkyVertex),count,f)==count);
        unsigned period=0,n=0;assert(std::fread(&period,4,1,f)==1&&std::fread(&n,4,1,f)==1);
        struct Key {unsigned tick;float rgba[4];};std::vector<Key> keys(n);assert(std::fread(keys.data(),sizeof(Key),n,f)==n);
        auto args=NorthlightSpellLayer::DrawArgs::primitiveUP(D3DPT_TRIANGLELIST,count/3,vertices.data(),sizeof(SkyVertex));
        unsigned visibleModes=0;
        for(unsigned mode=0;mode<2;++mode){auto* ps=pixels+(textures==2?2:0)+mode;auto* vs=model+(textures==2?1:0);
            NorthlightParticleLights::Frame frame;NorthlightLocalLightSelection::Tracker selection;
            for(unsigned k=0;k<4;++k)device.constants[28*4+k]=1;
            const auto full=capture.prepare(&device,vs,ps,args,inverse,camera,projection,frame,true);
            std::printf("SKY_BATCH packet=%u textures=%u mode=%u full=%u shaderMiss=%u geometryMiss=%u stateMiss=%u\n",packet,textures,mode,full.count,frame.shaderMiss,frame.geometryMiss,frame.stateMiss);
            assert(!frame.shaderMiss&&!frame.geometryMiss&&!frame.textureMiss&&!frame.stateMiss);
            if(!full.count)continue;++visibleModes;
            if(textures==2){
                const unsigned char black[4]={0,0,0,0},white[4]={255,255,255,255};auto& tex=device.secondTexture;
                NorthlightParticleTextures::cache().upload(&tex,&tex,1,1,0,D3DFMT_A8R8G8B8,black,4,4,true,true);frame.reset();
                assert(!capture.prepare(&device,vs,ps,args,inverse,camera,projection,frame,true).count);
                NorthlightParticleTextures::cache().upload(&tex,&tex,1,1,0,D3DFMT_A8R8G8B8,white,4,4,true,true);frame.reset();
                assert(capture.prepare(&device,vs,ps,args,inverse,camera,projection,frame,true).count);
                NorthlightParticleTextures::cache().upload(&tex,&tex,maskImage[0],maskImage[1],0,maskImage[2],maskBytes.data(),maskBytes.size(),maskImage[3],true,true);frame.reset();
            }
            capture.submit(full,true,false,frame);frame.projectSky(inverse,camera);frame.publish(nullptr,0,{},camera,selection);
            assert(frame.outputCount);const auto& l=frame.output[0];
            std::printf("SKY_SOURCE packet=%u mode=%u r=%.9g g=%.9g b=%.9g\n",packet,mode,l.diffuse[0],l.diffuse[1],l.diffuse[2]);
            unsigned lit=0,dark=0,peakTick=0;float peakEnergy=0,peakColour[3]{},peakRgba[4]{};
            for(const auto& key:keys){frame.nextFrame();std::copy_n(key.rgba,4,device.constants+28*4);
                const auto p=capture.prepare(&device,vs,ps,args,inverse,camera,projection,frame,true);
                assert(!frame.shaderMiss&&!frame.geometryMiss&&!frame.textureMiss&&!frame.stateMiss);
                capture.submit(p,true,false,frame);frame.projectSky(inverse,camera);frame.publish(nullptr,0,{},camera,selection,.016f);
                const auto selected=selection.update({},camera,8,.016f,true,nullptr,frame.output.data(),frame.outputCount,frame.immediate.data(),frame.immediateCount);
                if(key.rgba[3]==0||std::max({key.rgba[0],key.rgba[1],key.rgba[2]})==0){assert(!p.count&&!selected.count&&!selection.fading);++dark;}
                else if(frame.outputCount){assert(selected.count&&frame.outputCount<=2);++lit;
                    const auto& light=frame.output[0];const float power=NorthlightParticleLights::peak(light.diffuse);
                    if(power>peakEnergy){peakEnergy=power;peakTick=key.tick;std::copy_n(light.diffuse,3,peakColour);std::copy_n(key.rgba,4,peakRgba);}
                }
                ++timelines;
            }
            assert(lit);std::printf("SKY_KEYS packet=%u mode=%u period=%u samples=%u lit=%u dark=%u\n",packet,mode,period,n,lit,dark);
            std::printf("SKY_PEAK packet=%u mode=%u tick=%u r=%.9g g=%.9g b=%.9g tintR=%.9g tintG=%.9g tintB=%.9g alpha=%.9g\n",packet,mode,peakTick,peakColour[0],peakColour[1],peakColour[2],peakRgba[0],peakRgba[1],peakRgba[2],peakRgba[3]);
        }
        assert(visibleModes); // Every discovered sky batch must have a proven emitting native combiner.
    }
    assert(std::fgetc(f)==EOF);std::fclose(f);
    std::printf("PASS all sky flashes: batches=%u masked=%u timelineChecks=%u\n",cases,masked,timelines);
}
