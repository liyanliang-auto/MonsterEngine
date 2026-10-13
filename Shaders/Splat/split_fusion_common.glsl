// Copyright Monster Engine. All Rights Reserved.
// Runtime-only checkpoint ABI: 27 uvec4 per slot. Kernels are certificates,
// never old render attributes. Source and fitted kernels use the current view.
layout(std430,set=0,binding=0) buffer Radii { int radii[]; };
layout(std430,set=0,binding=1) buffer Depth { float depth[]; };
layout(std430,set=0,binding=2) buffer RGB { vec4 rgb[]; };
layout(std430,set=0,binding=3) buffer Conic { vec4 conic[]; };
layout(std430,set=0,binding=4) buffer XY { vec2 xy[]; };
layout(std430,set=0,binding=5) buffer Tiles { uint tiles[]; };
layout(std430,set=0,binding=6) buffer BBox { uvec4 bbox[]; };
layout(std430,set=0,binding=7) readonly buffer Groups { uvec2 groups[]; };
// rendered/fused/available/maxError/freshCount/freshOverflow/exactCount/
// exactOverflow/certPass/certFail/exactChecks/originalVisible, all same-frame.
layout(std430,set=0,binding=8) buffer Counter { uint counters[]; };
layout(std430,set=0,binding=9) coherent buffer Checkpoint { uvec4 checkpoint[]; };
layout(std430,set=0,binding=10) buffer FreshQueue { uint freshQueue[]; };
layout(std430,set=0,binding=11) buffer ExactQueue { uint exactQueue[]; };
layout(std430,set=0,binding=12) buffer FreshArgs { uint freshArgs[]; };
layout(std430,set=0,binding=13) buffer ExactArgs { uint exactArgs[]; };
layout(push_constant) uniform Push { uint groupCount; uint sourceCount; uint gridX; uint gridY; float maxError; uint cacheSlots; uint queueCapacity; uint frame; } pc;
const float TAU=6.283185307179586;
struct K { vec2 p; vec3 cov; float det; float z; float alpha; vec3 color; uint members; };
struct Gaussian { vec2 p; vec3 cov; float det; };
struct Term { Gaussian k; vec4 coeff; };
bool finite1(float x){return !isnan(x)&&!isinf(x);}
bool finite4(vec4 x){return all(not(isnan(x)))&&all(not(isinf(x)));}
vec3 inverseCov(K k){return vec3(k.cov.z,-k.cov.y,k.cov.x)/k.det;}
vec2 mul(vec3 m,vec2 p){return vec2(m.x*p.x+m.y*p.y,m.y*p.x+m.z*p.y);}
uvec2 interval(uint id){uint p=groups[id].x;return uvec2(p&0x0fffffffu,((p>>28u)&3u)+1u);}
uint base(uint id){return (groups[id].y%pc.cacheSlots)*27u;}
bool live(uint i){return radii[i]>0&&tiles[i]>0u;}
K readSource(uint i,uint lane,vec2 origin){vec3 q=conic[i].xyz;float d=q.x*q.z-q.y*q.y;K k;k.p=xy[i]-origin;k.cov=vec3(q.z,-q.y,q.x)/d;k.det=1/d;k.z=depth[i];k.alpha=conic[i].w;k.color=rgb[i].xyz;k.members=1u<<lane;return k;}
bool validK(K k){return k.det>0&&k.cov.x>0&&k.cov.z>0&&finite1(k.det)&&finite4(vec4(k.p,k.z,k.alpha))&&finite4(vec4(k.color,0))&&k.alpha>=0&&k.alpha<=1;}
K loadK(uint address){vec4 a=uintBitsToFloat(checkpoint[address]),b=uintBitsToFloat(checkpoint[address+1u]),c=uintBitsToFloat(checkpoint[address+2u]);K k;k.p=a.xy;k.cov=vec3(a.z,a.w,b.x);k.det=b.y;k.z=b.z;k.alpha=b.w;k.color=c.xyz;k.members=uint(c.w);return k;}
void saveK(uint address,K k){checkpoint[address]=floatBitsToUint(vec4(k.p,k.cov.xy));checkpoint[address+1u]=floatBitsToUint(vec4(k.cov.z,k.det,k.z,k.alpha));checkpoint[address+2u]=floatBitsToUint(vec4(k.color,float(k.members)));}
void release(uint id,bool owner){if(owner){memoryBarrierBuffer();atomicExchange(checkpoint[base(id)].w,0u);}}
void invalidate(uint id,bool owner){if(owner)checkpoint[base(id)].x=0u;release(id,owner);}
void enqueue(uint id,bool owner,bool exact){uint count=atomicAdd(counters[exact?6u:4u],1u);if(count>=pc.queueCapacity){atomicOr(counters[exact?7u:5u],1u);release(id,owner);return;}uint item=id|(owner?0x80000000u:0u);if(exact)exactQueue[count]=item;else freshQueue[count]=item;}
bool fit(K first,K second,out K result){
 if(second.z<first.z){K swapK=first;first=second;second=swapK;}
 vec3 ia=inverseCov(first),ib=inverseCov(second),A=ia+ib;vec2 ha=mul(ia,first.p),hb=mul(ib,second.p),h=ha+hb;float d=A.x*A.z-A.y*A.y;
 vec3 c=vec3(A.z,-A.y,A.x)/d;vec2 p=mul(c,h);float peak=first.alpha*second.alpha*exp(min(0.0,-.5*(dot(first.p,ha)+dot(second.p,hb)-dot(p,h))));
 float ma=TAU*sqrt(first.det)*first.alpha,mb=TAU*sqrt(second.det)*second.alpha,mab=TAU*sqrt(1/d)*peak,mass=ma+mb-mab;
 vec2 mean=ma*first.p+mb*second.p-mab*p;
 vec3 moment=ma*(first.cov+vec3(first.p.x*first.p.x,first.p.x*first.p.y,first.p.y*first.p.y))+mb*(second.cov+vec3(second.p.x*second.p.x,second.p.x*second.p.y,second.p.y*second.p.y))-mab*(c+vec3(p.x*p.x,p.x*p.y,p.y*p.y));
 vec3 color=ma*first.color+(mb-mab)*second.color;if(!(mass>0)||!finite1(mass))return false;
 result.p=mean/mass;p=result.p;result.cov=moment/mass-vec3(p.x*p.x,p.x*p.y,p.y*p.y);result.det=result.cov.x*result.cov.z-result.cov.y*result.cov.y;
 float intrinsic=(result.cov.x-.3)*(result.cov.z-.3)-result.cov.y*result.cov.y;if(!(result.cov.x>.3&&result.cov.z>.3&&intrinsic>0&&result.det>0)||!finite1(result.det))return false;
 result.alpha=min(.99,mass/(TAU*sqrt(result.det)));result.color=max(vec3(0),color/mass);result.z=clamp((ma*first.z+mb*second.z-mab*second.z)/mass,min(first.z,second.z),max(first.z,second.z));result.members=first.members|second.members;return validK(result);
}
bool refit(K original[4],uint ns,uint tree,out K current[4],out uint n){n=ns;for(uint i=0;i<ns;++i)current[i]=original[i];for(uint step=0;step<3u;++step){uint op=(tree>>(8u*step))&255u;if(op==0u)break;uint left=op&15u,right=op>>4u,a=4u,b=4u;for(uint i=0;i<n;++i){if(current[i].members==left)a=i;if(current[i].members==right)b=i;}K merged;if(a==4u||b==4u||a==b||!fit(current[a],current[b],merged))return false;uint count=0;for(uint i=0;i<n;++i)if(i!=a&&i!=b)current[count++]=current[i];current[count++]=merged;n=count;}return true;}
bool orderMatches(K current[4],uint n,uint address){for(uint i=0;i<n;++i)for(uint j=i+1u;j<n;++j){float a=loadK(address+i*3u).z,b=loadK(address+j*3u).z;if((a<b)!=(current[i].z<current[j].z)||(a>b)!=(current[i].z>current[j].z))return false;}return true;}
bool bounds(K k,vec2 origin,out uvec4 box,out int radius){vec2 p=k.p+origin;float mid=(k.cov.x+k.cov.z)*.5,r=ceil(3*sqrt(mid+sqrt(max(.1,mid*mid-k.det))));if(!finite1(r)||!finite4(vec4(p,0,0)))return false;uvec2 lo=uvec2(clamp((p-r)/16,vec2(0),vec2(pc.gridX,pc.gridY))),hi=uvec2(clamp((p+r+15)/16,vec2(0),vec2(pc.gridX,pc.gridY)));box=uvec4(lo,hi);radius=int(min(r,2147483520.0));return radius>0&&hi.x>lo.x&&hi.y>lo.y;}
bool emit(uint id,K current[4],uint n){uvec2 g=interval(id);vec2 origin=xy[g.x];uvec4 boxes[4];int sizes[4];for(uint i=0;i<n;++i)if(bitCount(current[i].members)>1&&!bounds(current[i],origin,boxes[i],sizes[i]))return false;
 uint fused=0;for(uint i=0;i<n;++i){K k=current[i];if(bitCount(k.members)<2)continue;++fused;uint lane=uint(findLSB(k.members)),index=g.x+lane;for(uint j=0;j<g.y;++j)if(j!=lane&&(k.members&(1u<<j))!=0u){radii[g.x+j]=0;tiles[g.x+j]=0u;bbox[g.x+j]=uvec4(0);}radii[index]=sizes[i];depth[index]=k.z;xy[index]=k.p+origin;rgb[index]=vec4(k.color,0);conic[index]=vec4(inverseCov(k),k.alpha);bbox[index]=boxes[i];tiles[index]=(boxes[i].z-boxes[i].x)*(boxes[i].w-boxes[i].y);}
 atomicAdd(counters[0],0u-(g.y-n));atomicAdd(counters[1],fused);return true;
}
void store(uint id,bool owner,uint tree,K original[4],uint ns,K current[4],uint n,vec4 norm,vec4 error){if(!owner)return;uint address=base(id);checkpoint[address].y=tree;checkpoint[address].z=pc.frame;checkpoint[address+1u]=floatBitsToUint(norm);checkpoint[address+2u]=floatBitsToUint(error);for(uint i=0;i<ns;++i)saveK(address+3u+i*3u,original[i]);for(uint i=0;i<n;++i)saveK(address+15u+i*3u,current[i]);checkpoint[address].x=groups[id].y+1u;}
float integral(Gaussian a,Gaussian b){vec3 c=a.cov+b.cov;float d=c.x*c.z-c.y*c.y;vec2 p=a.p-b.p;return TAU*sqrt(a.det*b.det/d)*exp(-.5*dot(p,mul(vec3(c.z,-c.y,c.x)/d,p)));}
void field(K inputK[4],uint n,out Term terms[15],out uint size){K ks[4];for(uint i=0;i<n;++i)ks[i]=inputK[i];for(uint i=1;i<n;++i)for(uint j=i;j>0u&&ks[j].z<ks[j-1u].z;--j){K k=ks[j];ks[j]=ks[j-1u];ks[j-1u]=k;}vec3 inv[4];for(uint i=0;i<n;++i)inv[i]=inverseCov(ks[i]);size=(1u<<n)-1u;for(uint mask=1u;mask<=size;++mask){vec3 A=vec3(0);vec2 h=vec2(0);float q=0,w=1;uint cnt=0,last=0;for(uint i=0;i<n;++i)if((mask&(1u<<i))!=0u){vec2 ap=mul(inv[i],ks[i].p);A+=inv[i];h+=ap;q+=dot(ks[i].p,ap);w*=ks[i].alpha;++cnt;last=i;}float d=A.x*A.z-A.y*A.y;Term t;t.k.cov=vec3(A.z,-A.y,A.x)/d;t.k.det=1/d;t.k.p=mul(t.k.cov,h);w*=exp(min(0.0,-.5*(q-dot(t.k.p,h))));if((cnt&1u)==0u)w=-w;t.coeff=w*vec4(ks[last].color,1);terms[mask-1u]=t;}}
vec4 squaredNorm(Term terms[15],uint n){vec4 sum=vec4(0);for(uint i=0;i<n;++i){sum+=terms[i].coeff*terms[i].coeff*(.5*TAU*sqrt(terms[i].k.det));for(uint j=i+1u;j<n;++j)sum+=2*terms[i].coeff*terms[j].coeff*integral(terms[i].k,terms[j].k);}return sum;}
vec4 inner(Term a[15],uint na,Term b[15],uint nb){vec4 sum=vec4(0);for(uint i=0;i<na;++i)for(uint j=0;j<nb;++j)sum+=a[i].coeff*b[j].coeff*integral(a[i].k,b[j].k);return sum;}
float relative(vec4 difference,vec4 norm){if(!finite4(difference)||!finite4(norm)||any(lessThanEqual(norm,vec4(0))))return 1.0/0.0;vec4 r=difference/norm;return max(max(r.x,r.y),max(r.z,r.w));}
