static const NorthlightDisplacementGeometry::Height* reference(NorthlightAutoDisplacement& self,IDirect3DTexture9* source,bool model){using Height=NorthlightDisplacementGeometry::Height;using Texture=NorthlightDisplacementStorage::Texture;auto& textures=self.textures;auto& clock=self.clock;auto& readsThisFrame=self.readsThisFrame;auto& polarity=self.polarity;auto& heightDetails=self.heightDetails;auto& heightBalance=self.heightBalance;

        NorthlightMaterialProfile::Scope timing(self.profile,NorthlightMaterialProfile::HeightRead);
        const auto revision=NorthlightMaterialHeight::version(source);
        for(auto it=textures.begin();it!=textures.end();++it)if(it->source==source&&it->model==model){if(it->version!=revision){drop(it->gpu);drop(it->source);textures.erase(it);break;}it->used=++clock;return it->height.width?&it->height:nullptr;}
        if(readsThisFrame>=2)return nullptr;++readsThisFrame;
        auto reject=[&]()->const Height*{if(textures.size()>=32){auto old=std::min_element(textures.begin(),textures.end(),[](const Texture& a,const Texture& b){return a.used<b.used;});drop(old->gpu);drop(old->source);textures.erase(old);}Texture failed;failed.source=source;source->AddRef();failed.used=++clock;failed.version=revision;failed.model=model;textures.push_back(std::move(failed));return nullptr;};
        NorthlightMaterialHeight::Image image;if(!NorthlightMaterialHeight::read(source,image,!model,polarity==0&&heightDetails!=0))return reject();
        if(textures.size()>=32){auto oldest=std::min_element(textures.begin(),textures.end(),[](const Texture& a,const Texture& b){return a.used<b.used;});drop(oldest->gpu);drop(oldest->source);textures.erase(oldest);}
        Texture item;item.source=source;source->AddRef();item.used=++clock;item.version=revision;item.model=model;item.height.width=image.width;item.height.height=image.height;item.height.values.resize(size_t(image.width)*image.height);
        const auto profile=NorthlightMaterialHeight::profile(image,polarity,heightDetails,heightBalance);
        if(model)NorthlightMaterialHeight::modelValues(image,polarity,item.height.values);
        else if(profile[1]==0)for(size_t i=0;i<item.height.values.size();++i)item.height.values[i]=NorthlightMaterialHeight::valueWithoutAccent(image.rgba[i*4]/255.f,image.rgba[i*4+1]/255.f,image.rgba[i*4+2]/255.f,profile);
        else for(size_t i=0;i<item.height.values.size();++i)item.height.values[i]=NorthlightMaterialHeight::value(image.rgba[i*4]/255.f,image.rgba[i*4+1]/255.f,image.rgba[i*4+2]/255.f,profile);
        textures.push_back(std::move(item));return &textures.back().height;
    }
