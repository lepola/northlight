// Record the production receiver point, independently of shadow visibility.
float4 TransitionWorldPoint(float2 uv:TEXCOORD0):COLOR0 {
 float2 screen=uv;uv=depthUV(uv);float depth=normalizedDepth(uv);
 float4 n=tex2Dlod(NormalBuffer,float4(screen,0,0));
 return float4(worldPosition(uv,depth)+n.xyz*(.08*n.w),1);
}

struct ProjectionPair {float4 nearPoint:COLOR0;float4 farPoint:COLOR1;};
ProjectionPair projectedReceiver(float2 uv,bool previous,bool lift){
 float2 screen=uv;uv=depthUV(uv);float depth=normalizedDepth(uv);
 float4 n=tex2Dlod(NormalBuffer,float4(screen,0,0));
 float3 p=worldPosition(uv,depth)+n.xyz*(.08*n.w);
 float nearFacing=dot(n.xyz,previous?PreviousSunDirection.xyz:CurrentNearDirection.xyz);
 float farFacing=dot(n.xyz,previous?PreviousFarDirection.xyz:CurrentFarDirection.xyz);
 float nearLift=lift?(1-saturate(abs(nearFacing)))*n.w*2/1024:0;
 float farLift=lift?(1-saturate(abs(farFacing)))*n.w*2/1024:0;
 ProjectionPair o;
 o.nearPoint=float4(affinePoint(p+n.xyz*(nearLift*DepthParams.y),previous?PreviousNearMatrix:NearMatrix),1);
 o.farPoint=float4(affinePoint(p+n.xyz*(farLift*DepthParams.z),previous?PreviousFarMatrix:FarMatrix),1);
 return o;
}
ProjectionPair TransitionCurrentPoints(float2 uv:TEXCOORD0){return projectedReceiver(uv,false,true);}
ProjectionPair TransitionPreviousPoints(float2 uv:TEXCOORD0){return projectedReceiver(uv,true,true);}
ProjectionPair TransitionPreviousCoverage(float2 uv:TEXCOORD0){return projectedReceiver(uv,true,false);}

// Uploaded query coordinates isolate the filter from native SM3 affine/RCP
// rounding across separately compiled entry points. Production kernels remain
// the code under test; the scalar oracle owns comparisons and all tap weights.
sampler2D QueryCurrentNear:register(s0);
sampler2D QueryCurrentFar:register(s1);
sampler2D QueryPreviousNear:register(s4);
sampler2D QueryPreviousFar:register(s5);
sampler2D QueryCoverNear:register(s10);
sampler2D QueryCoverFar:register(s11);
float queryVisibility(float2 uv,bool previous){
 float3 cn=tex2Dlod(QueryCurrentNear,float4(uv,0,0)).xyz,cf=tex2Dlod(QueryCurrentFar,float4(uv,0,0)).xyz;
 float3 pn=tex2Dlod(QueryPreviousNear,float4(uv,0,0)).xyz,pf=tex2Dlod(QueryPreviousFar,float4(uv,0,0)).xyz;
 float3 nc=tex2Dlod(QueryCoverNear,float4(uv,0,0)).xyz,fc=tex2Dlod(QueryCoverFar,float4(uv,0,0)).xyz;
 bool nh=!any(abs(nc*float3(1,1,2)-float3(0,0,1))>.99),fh=!any(abs(fc*float3(1,1,2)-float3(0,0,1))>.99);
 float4 n=tex2Dlod(NormalBuffer,float4(uv,0,0));
 float blend=lerp(saturate((max(abs(pn.x),abs(pn.y))-.72)/.18),saturate((max(abs(cn.x),abs(cn.y))-.72)/.18),ShadowTransition.x),result=0;
 [loop]for(int cascade=0;cascade<2;++cascade){
  bool nearCascade=cascade==0,history=previous&&(nearCascade?nh:fh);float radius=nearCascade?DepthParams.y:DepthParams.z;
  float weight=(nearCascade?1-blend:blend)*(previous?1-(nearCascade?ShadowTransition.x:ShadowTransition.y):(nearCascade?ShadowTransition.x:ShadowTransition.y));
  [branch]if(weight>0){
   float3 q=history?(nearCascade?pn:pf):(nearCascade?cn:cf);
   row_major float4x4 m=history?(nearCascade?PreviousNearMatrix:PreviousFarMatrix):(nearCascade?CurrentNearMatrix:CurrentFarMatrix);
   float3 direction=history?(nearCascade?PreviousSunDirection.xyz:PreviousFarDirection.xyz):(nearCascade?CurrentNearDirection.xyz:CurrentFarDirectionFallback.xyz);
   float2 plane=transitionPlane(n.xyz,n.w,dot(n.xyz,direction),radius,m);
   float visibility=previous?transitionShadowMap(q,nearCascade,history,plane*radius,ShadowTransition.z>.5):shadowMapFiltered(q,nearCascade,radius,plane*radius,ShadowTransition.z>.5);
   result+=visibility*weight;
  }
 }
 return result;
}
float4 TransitionCurrentQuery(float2 uv:TEXCOORD0):COLOR0{return queryVisibility(uv,false).xxxx;}
float4 TransitionPreviousQuery(float2 uv:TEXCOORD0):COLOR0{return queryVisibility(uv,true).xxxx;}
