#define main capture_regression_main
#include "test_particle_light_capture.cpp"
#undef main

int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    assert(argc==3);FILE* f=std::fopen(argv[1],"rb");assert(f);
    Shader vs,modelVS;vs.code=words(f);modelVS.code=words(f);const auto psCode=words(f);Pixel ps;Device device;
    assert(family(modelVS)==3);
    NorthlightParticleCapture::Capture capture;capture.registerVertex(&vs,family(vs));capture.registerVertex(&modelVS,3);capture.registerPixel(&ps,psCode.data(),psCode.size());
    assert(capture.catalog.read(argv[2])&&capture.catalog.size());
    NorthlightParticleTextures::enabled=true;
    unsigned models=0;assert(std::fread(&models,4,1,f)==1&&models==4);
    const float inverse[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},camera[3]{},projection[3]={1,1,1};
    unsigned timelineCases=0;
    for(unsigned model=0;model<models;++model){
        unsigned image[5];assert(std::fread(image,4,5,f)==5);std::vector<unsigned char> bytes(image[4]);assert(std::fread(bytes.data(),1,bytes.size(),f)==bytes.size());
        NorthlightParticleTextures::cache().upload(&device.texture,&device.texture,image[0],image[1],0,image[2],bytes.data(),bytes.size(),image[3],true,true);
        unsigned count=0;assert(std::fread(&count,4,1,f)==1&&count&&count%3==0);std::vector<Vertex> original(count);
        assert(std::fread(original.data(),sizeof(Vertex),count,f)==count);
        const bool sky=model>=2;
        unsigned period=0,frameCount=0;assert(std::fread(&period,4,1,f)==1&&std::fread(&frameCount,4,1,f)==1);
        struct Key {unsigned tick;float rgba[4];};
        std::vector<Key> keys(frameCount);for(auto& key:keys)assert(std::fread(&key,sizeof(Key),1,f)==1);
        float weak=0;
        for(bool fixed:{false,true})for(float alpha:{0.f,.25f,1.f}){
            auto vertices=original;for(auto& v:vertices){if(!sky)v.position[2]+=400;v.color=D3DCOLOR_ARGB(unsigned(alpha*255),255,255,255);}
            const auto args=NorthlightSpellLayer::DrawArgs::primitiveUP(D3DPT_TRIANGLELIST,count/3,vertices.data(),sizeof(Vertex));
            device.destination=D3DBLEND_INVSRCALPHA;
            NorthlightParticleLights::Frame frame;NorthlightLocalLightSelection::Tracker selection;
            for(unsigned k=0;k<4;++k)device.constants[28*4+k]=k==3?float(unsigned(alpha*255))/255:1;
            const auto p=capture.prepare(&device,fixed?nullptr:sky?&modelVS:&vs,fixed?nullptr:&ps,args,inverse,camera,projection,frame,sky);
            capture.submit(p,true,false,frame);frame.projectSky(inverse,camera);frame.publish(nullptr,0,{},camera,selection);
            if(alpha==0){assert(!p.count&&!frame.outputCount);continue;}
            std::printf("EMISSION actualBolt=%u fixed=%u alpha=%.2f prepared=%u output=%u range=%g distant=%u shader=%u geometry=%u tex=%u state=%u triangles=%u\n",model,fixed,alpha,p.count,frame.outputCount,frame.outputCount?frame.output[0].attenuationEnd:0,frame.distant,frame.shaderMiss,frame.geometryMiss,frame.textureMiss,frame.stateMiss,frame.triangles);
            assert(p.count&&frame.outputCount&&frame.outputCount<=2);
            const float energy=NorthlightParticleLights::peak(frame.output[0].diffuse);
            if(alpha<.5f)weak=energy;else assert(energy>weak*3.9f&&energy<weak*4.2f);
            for(unsigned i=0;i<frame.outputCount;++i){const auto& l=frame.output[i];assert(l.flags==NorthlightLocalLights::Flash&&NorthlightLocalLights::valid(l));
                assert(NorthlightLocalLights::attenuation(l,50)>NorthlightLocalLights::attenuation(l,100));
                assert(NorthlightLocalLights::attenuation(l,l.attenuationEnd)==0);}
            const auto lit=selection.update({},camera,8,.016f,true,nullptr,frame.output.data(),frame.outputCount,frame.immediate.data(),frame.immediateCount);
            assert(lit.count&&lit.color[0][3]<0);
            frame.nextFrame();frame.publish(nullptr,0,{},camera,selection);
            assert(!selection.update({},camera,8,.016f,true,nullptr,frame.output.data(),frame.outputCount,frame.immediate.data(),frame.immediateCount).count);
        }
        if(sky){
            assert(period&&frameCount>60);NorthlightParticleLights::Frame frame;NorthlightLocalLightSelection::Tracker selection;
            const auto args=NorthlightSpellLayer::DrawArgs::primitiveUP(D3DPT_TRIANGLELIST,count/3,original.data(),sizeof(Vertex));
            device.destination=D3DBLEND_ONE;
            unsigned dark=0,lit=0,previous=0;float reference=0;
            for(unsigned k=0;k<4;++k)device.constants[28*4+k]=1;
            const auto full=capture.prepare(&device,&modelVS,&ps,args,inverse,camera,projection,frame,true);
            assert(full.count);for(unsigned i=0;i<full.count;++i)reference=std::max(reference,NorthlightParticleLights::brightness(full.clusters[i].sample));
            for(const auto& key:keys){
                frame.nextFrame();std::copy_n(key.rgba,4,device.constants+28*4);
                const auto p=capture.prepare(&device,&modelVS,&ps,args,inverse,camera,projection,frame,true);
                capture.submit(p,true,false,frame);frame.projectSky(inverse,camera);frame.publish(nullptr,0,{},camera,selection,.016f);
                const auto selected=selection.update({},camera,8,.016f,true,nullptr,frame.output.data(),frame.outputCount,frame.immediate.data(),frame.immediateCount);
                if(key.rgba[3]==0){assert(!p.count&&!frame.outputCount&&!selected.count&&!selection.fading);++dark;}
                else if(key.rgba[3]>.1f){assert(p.count&&frame.outputCount&&selected.count);++lit;
                    float energy=0;for(unsigned i=0;i<p.count;++i)energy=std::max(energy,NorthlightParticleLights::brightness(p.clusters[i].sample));
                    assert(std::fabs(energy-reference*key.rgba[3])<.00001f);
                }
                assert(key.tick>=previous);previous=key.tick;++timelineCases;
            }
            assert(dark&&lit);std::printf("SKY_TIMELINE model=%u periodMs=%u samples=%u dark=%u lit=%u nativeModelShader=1 zeroTail=1\n",model,period,frameCount,dark,lit);
            frame.reset();device.constants[28*4+3]=0;
            for(unsigned i=0;i<20;++i)assert(!capture.prepare(&device,&modelVS,&ps,args,inverse,camera,projection,frame,true).count);
            device.constants[28*4+3]=1;
            assert(capture.prepare(&device,&modelVS,&ps,args,inverse,camera,projection,frame,true).count);
        }
    }
    // View-space sky directions are rotated only after current camera recovery.
    // A translated/rotated camera must reconstruct the same world discharge.
    using namespace NorthlightParticleLights;
    const auto skyLight=[&](const float* viewDirection,const float* matrix,const float* eye){
        Frame frame;NorthlightLocalLightSelection::Tracker selection;Sample s{{viewDirection[0],viewDirection[1],viewDirection[2]},{.2f,.4f,1}};
        frame.observeSky(s);frame.projectSky(matrix,eye);frame.publish(nullptr,0,{},eye,selection);assert(frame.outputCount==1);return frame.output[0];
    };
    const float up[3]={0,0,1},horizon[3]={1,0,0},zero[3]{},translated[3]={1000,-500,20};
    const auto nearbyBolt=skyLight(up,inverse,zero),horizonBolt=skyLight(horizon,inverse,zero);
    assert(std::fabs(nearbyBolt.position[2]-350)<.01f&&std::fabs(horizonBolt.position[0]-1750)<.01f);
    assert(NorthlightLocalLights::attenuation(nearbyBolt,350)>NorthlightLocalLights::attenuation(horizonBolt,1750));
    const float rotated[16]={0,0,1,0,0,1,0,0,-1,0,0,0,1000,-500,20,1},viewUp[3]={1,0,0};
    const auto recovered=skyLight(viewUp,rotated,translated);
    for(unsigned k=0;k<3;++k)assert(std::fabs(recovered.position[k]-translated[k]-nearbyBolt.position[k])<.01f);
    {Frame frame;NorthlightLocalLightSelection::Tracker selection;
     for(unsigned warm=0;warm<3;++warm){frame.nextFrame();
         for(unsigned i=0;i<32;++i)frame.observe(Sample{{float(i)*1.5f,float(i%2)*2,2},{1,.1f,.1f}},zero);
         if(warm){frame.observeSky(Sample{{0,0,1},{.2f,.4f,1}});frame.observeSky(Sample{{1,0,.4f},{.4f,.2f,1}});frame.projectSky(inverse,zero);}
         frame.publish(nullptr,0,{},zero,selection);
         assert(frame.outputCount==8&&unsigned(std::count_if(frame.output.begin(),frame.output.begin()+frame.outputCount,[](const auto& l){return l.flags&NorthlightLocalLights::Flash;}))==(warm?2u:0u));
     }
     frame.nextFrame();
     for(unsigned i=0;i<4;++i)assert(frame.takeDraw(true,true));assert(!frame.takeDraw(true,true));
     frame.reset();frame.observeSky(Sample{{NAN,0,1},{1,1,1}});frame.observeSky(Sample{{0,0,0},{1,1,1}});frame.projectSky(inverse,zero);frame.publish(nullptr,0,{},zero,selection);assert(!frame.outputCount);
    }
    assert(std::fgetc(f)==EOF);
    std::fclose(f);std::printf("PASS lightning: nativeTimelineCases=%u camera/near/horizon/budgets/invalid/lifecycle=PASS\n",timelineCases);
}
