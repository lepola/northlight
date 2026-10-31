// Exact template specialization for the client's explicit four-weight palette
// blend. This is a bound on the ORIGINAL geometry; no shadow mesh is simplified.
// All other programs retain the general interval evaluator.
struct SkinEnvelope {
    struct Bone {float low[3]={},high[3]={};bool used=false;};
    Bone bones[75];unsigned usedBones=0;unsigned char order[75]={}; /* used bones in first-use order */double sumLow=INFINITY,sumHigh=-INFINITY,maxPosition[4]={0,0,0,1};
    static bool supports(const NorthlightActorDeformation::Program& p){
        if((p.major!=2&&p.major!=3)||p.positionRegister>1||p.textureCoordinates||p.operations.size()!=18||p.inputs.size()!=3)return false;
        bool inputs[3]={};for(const auto& i:p.inputs){if(i.reg>=3||i.usage!=i.reg||i.index)return false;inputs[i.reg]=true;}if(!inputs[0]||!inputs[1]||!inputs[2])return false;
        bool def=false;for(const auto& d:p.definitions)if(d.reg==0){if(d.value[0]!=3||d.value[1]!=1)return false;def=true;}if(!def)return false;
        // Stock body shaders use r0 or r1 for position. The independent w=1
        // write can precede or follow the XYZ skinning without changing it.
        const unsigned position=p.positionRegister,temporary=1-position;
        const NorthlightActorDeformation::Operation* ops[17];size_t count=0;bool w=false;
        for(const auto& o:p.operations){
            if(o.code==1&&o.destination==(0x80080000u|position)&&o.source[0].token==0xa0550000&&!o.source[0].address&&
               !o.source[1].token&&!o.source[1].address&&!o.source[2].token&&!o.source[2].address){if(w)return false;w=true;}
            else{if(count==17)return false;ops[count++]=&o;}}
        if(!w||count!=17)return false;
        size_t at=0;
        const auto match=[&](unsigned code,std::uint32_t dst,std::uint32_t a,std::uint32_t b=0,std::uint32_t c=0,std::uint32_t aa=0,std::uint32_t ba=0){
            if(at>=count)return false;const auto& o=*ops[at++];
            return o.code==code&&o.destination==dst&&o.source[0].token==a&&o.source[0].address==aa&&o.source[1].token==b&&o.source[1].address==ba&&o.source[2].token==c&&o.source[2].address==0;
        };
        const bool commuted=ops[0]->source[0].token==0x90e40002;
        if(!match(5,0x800f0000u|temporary,commuted?0x90e40002:0xa0000000,commuted?0xa0000000:0x90e40002)||!match(46,0xb00f0000,0x80e40000u|temporary))return false;
        for(unsigned row=0;row<3;++row){const unsigned r=row?2:temporary;const std::uint32_t dst=0x800f0000|r,src=0x80e40000|r,matrix=0xa0e4201f+row;
            if(!match(5,dst,0x90550001,matrix,0,0,0xb0550000)||
               !match(4,dst,matrix,0x90000001,src,0xb0000000)||
               !match(4,dst,matrix,0x90aa0001,src,0xb0aa0000)||
               !match(4,dst,matrix,0x90ff0001,src,0xb0ff0000)||
               !match(9,0x80000000u|position|(1u<<(16+row)),src,0x90e40000))return false;
        }
        return at==count;
    }
    bool add(const NorthlightActorDeformation::Four inputs[16]){
        const auto& position=inputs[0];const auto& weights=inputs[1];const auto& indices=inputs[2];
        if(position[3]!=1)return false;for(float f:position)if(!std::isfinite(f)||(f!=0&&std::fabs(f)<std::numeric_limits<float>::min()))return false;
        double sumLo=0,sumHi=0;
        for(unsigned slot=0;slot<4;++slot){if(!std::isfinite(weights[slot])||weights[slot]<0||(weights[slot]!=0&&weights[slot]<std::numeric_limits<float>::min())||!std::isfinite(indices[slot])||indices[slot]<0||indices[slot]>74||std::floor(indices[slot])!=indices[slot])return false;
            if(sumHi+weights[slot]>0){sumLo=std::nextafter(sumLo+double(weights[slot]),-INFINITY);sumHi=std::nextafter(sumHi+double(weights[slot]),INFINITY);}}
        if(!std::isfinite(sumHi)||sumHi>16)return false; // Pathological inputs stay on the general evaluator.
        sumLow=std::min(sumLow,std::max(0.,sumLo));sumHigh=std::max(sumHigh,sumHi);
        for(unsigned axis=0;axis<3;++axis)maxPosition[axis]=std::max(maxPosition[axis],std::fabs(double(position[axis])));
        for(unsigned slot=0;slot<4;++slot){auto& b=bones[unsigned(indices[slot])];
            for(unsigned axis=0;axis<3;++axis){if(!b.used)b.low[axis]=b.high[axis]=position[axis];else{b.low[axis]=std::min(b.low[axis],position[axis]);b.high[axis]=std::max(b.high[axis],position[axis]);}}if(!b.used)order[usedBones++]=(unsigned char)(unsigned(indices[slot]));b.used=true;
        }return true;
    }
    bool evaluate(const float* constants,const float* inverse,Bounds& output,bool(*shouldStop)(void*)=nullptr,void* stopContext=nullptr)const{
        output={};if(!constants||!inverse)return false;
        for(unsigned i=0;i<16;++i)if(!std::isfinite(inverse[i])||(inverse[i]!=0&&std::fabs(inverse[i])<std::numeric_limits<float>::min()))return false;
        if(inverse[3]!=0||inverse[7]!=0||inverse[11]!=0||inverse[15]!=1)return false;
        if(!std::isfinite(sumLow)||!std::isfinite(sumHigh)||sumLow<0||sumHigh<sumLow||sumHigh>16)return false;
        double low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY},magnitude[3]={};
        double largest[3][4]={};bool any=false;unsigned checkedBones=0;
        if(FastReady){
            /* Same values in fewer branches: visit only used bones, validate the
               12 row floats with bit tests, keep |row| maxima in float (fabs and
               max are exact). Per-axis low/high use the unchanged double sums,
               and min/max over bones is order-independent: identical bits. */
            float largestF[12]={};std::uint32_t bad=0;double lo[3]={low[0],low[1],low[2]},hi[3]={high[0],high[1],high[2]};
            for(unsigned n=0;n<usedBones;++n){const unsigned bone=order[n];const auto& b=bones[bone];
                if(n&&(n&31u)==0&&shouldStop&&shouldStop(stopContext))return false;any=true;
                const float* rows=constants+4*(31+3*bone);std::uint32_t bits[12];std::memcpy(bits,rows,48);
                for(unsigned k=0;k<12;++k){const std::uint32_t u=bits[k]&0x7fffffffu;bad|=std::uint32_t(u>=0x7f800000u)|std::uint32_t(u-1u<0x007fffffu);largestF[k]=std::max(largestF[k],std::fabs(rows[k]));}
                const double l0=b.low[0],l1=b.low[1],l2=b.low[2],h0=b.high[0],h1=b.high[1],h2=b.high[2];
                for(unsigned axis=0;axis<3;++axis){const float* row=rows+4*axis;const double r0=row[0],r1=row[1],r2=row[2];
                    const double x0=r0*l0,y0=r0*h0,x1=r1*l1,y1=r1*h1,x2=r2*l2,y2=r2*h2;
                    const double a=((double(row[3])+std::min(x0,y0))+std::min(x1,y1))+std::min(x2,y2);
                    const double c=((double(row[3])+std::max(x0,y0))+std::max(x1,y1))+std::max(x2,y2);
                    lo[axis]=std::min(lo[axis],a);hi[axis]=std::max(hi[axis],c);
                }
            }
            for(unsigned axis=0;axis<3;++axis){low[axis]=lo[axis];high[axis]=hi[axis];}
            if(bad)return false;
            for(unsigned axis=0;axis<3;++axis)for(unsigned j=0;j<4;++j)largest[axis][j]=largestF[4*axis+j];
        }else
        for(unsigned bone=0;bone<75;++bone){const auto& b=bones[bone];if(!b.used)continue;
            if((checkedBones++&7u)==0&&shouldStop&&shouldStop(stopContext))return false;any=true;
            for(unsigned axis=0;axis<3;++axis){const float* row=constants+4*(31+3*bone+axis);double lo=row[3],hi=row[3];
                for(unsigned j=0;j<4;++j){if(!std::isfinite(row[j])||(row[j]!=0&&std::fabs(row[j])<std::numeric_limits<float>::min()))return false;largest[axis][j]=std::max(largest[axis][j],std::fabs(double(row[j])));}
                for(unsigned j=0;j<3;++j){double x=double(row[j])*b.low[j],y=double(row[j])*b.high[j];lo+=std::min(x,y);hi+=std::max(x,y);}
                low[axis]=std::min(low[axis],lo);high[axis]=std::max(high[axis],hi);
            }
        }
        if(!any)return false;detail::Vector view={};view[3]={1,0};
        for(unsigned axis=0;axis<3;++axis){
            // w_s >= 0 proves convex-hull enclosure after division by S=sum(w).
            // S is retained as an interval, not assumed to equal one. S=0 is safe.
            const double values[]={low[axis]*sumLow,low[axis]*sumHigh,high[axis]*sumLow,high[axis]*sumHigh};
            double lo=*std::min_element(values,values+4),hi=*std::max_element(values,values+4);
            double positionScale=1;for(unsigned j=0;j<4;++j){
                if(largest[axis][j]*sumHigh>double(std::numeric_limits<float>::max())/32)return false;
                magnitude[axis]+=largest[axis][j]*maxPosition[j];positionScale+=maxPosition[j];
            }magnitude[axis]*=sumHigh;
            // A term traverses <=8 float roundings (4-term MUL/MAD row blend,
            // followed by DP4). 64*epsilon exceeds gamma_16 with ample room for
            // dot reassociation, host-double summation and FTZ at each step.
            // The absolute-product sum bounds cancellation independently of pose.
            const double error=64*std::numeric_limits<float>::epsilon()*magnitude[axis]+1024*std::numeric_limits<float>::min()*positionScale;
            if(!std::isfinite(magnitude[axis])||magnitude[axis]>double(std::numeric_limits<float>::max())/32)return false;
            lo-=error;hi+=error;const float mid=float((lo+hi)*.5);
            view[axis]={mid,std::max(double(mid)-lo,hi-double(mid))};
        }
        for(unsigned axis=0;axis<3;++axis){detail::Value world={inverse[12+axis],0};double scale=std::fabs(world.value);
            for(unsigned j=0;j<3;++j){auto term=detail::mul(view[j],{inverse[4*j+axis],0});scale+=std::fabs(term.value)+term.error;world=detail::add(world,term);}
            world.error+=detail::roundError(scale)*8+.001;
            if(!std::isfinite(world.value)||!std::isfinite(world.error)||std::fabs(world.value)+world.error>1000000)return false;
            output.low[axis]=std::nextafter(float(double(world.value)-world.error),-std::numeric_limits<float>::infinity());
            output.high[axis]=std::nextafter(float(double(world.value)+world.error),std::numeric_limits<float>::infinity());
        }output.valid=true;return true;
    }
};
