#pragma once
#include <string>
namespace grpp {
// Each instance emits one arm; atlas cells reuse the same arm geometry.
inline const char *gridGeometry = R"GRID(
int primitiveCount(vec4 shape,int mode){int n=int(shape.w);if(mode==0)return 2;if(mode==1)return 4*n;int rows=mode==3?2:1;return 2*rows*(abs(shape.x-shape.y)<1.e-8?2:1+n);}
vec3 shadeColor(vec3 increments,int i){uvec3 q=uvec3(increments*float(i+1))>>16;uvec3 bits=q/uvec3(8,4,8);return vec3((bits<<uvec3(3,2,3))|(bits>>uvec3(2,4,2)))/255.0;}
void primitive(int id,vec4 shape,vec3 increments,int mode,out vec2 a,out vec2 b,out vec3 tint,out float energy,out bool ramp){
 float rx=shape.x,ry=shape.y,n=shape.w,stepX=rx/n,stepY=ry/n;int count=int(n);energy=shape.z;ramp=false;
 if(mode==0){a=id==0?vec2(0,-ry):vec2(-rx,0);b=-a;tint=shadeColor(increments,0);return;}
 if(mode==1){int shade=id/4,side=id%4;float i=float(shade);a=side==0?vec2(-rx+i*stepX,0):side==1?vec2(rx-i*stepX,0):side==2?vec2(0,-ry+i*stepY):vec2(0,ry-i*stepY);b=a;tint=shadeColor(increments,shade);energy*=min(1.0,side<2?stepX:stepY)*2.5;return;}
 int rows=mode==3?2:1,horizontal=2*rows;tint=shadeColor(increments,count-1);
 if(id<horizontal){float y=mode==3?(id<2?-ry:ry):0.0;a=vec2(id%2==0?-rx:rx,y);b=vec2(0,y);ramp=true;return;}
 int v=id-horizontal;
 if(abs(rx-ry)<1.e-8){float x=mode==3?(v<2?-rx:rx):0.0;a=vec2(x,v%2==0?-ry:ry);b=vec2(x,0);ramp=true;return;}
 int shade=v/(2*rows),side=v%(2*rows);float x=mode==3?(side<2?-rx:rx):0.0,i=float(shade);
 float y=side%2==0?-ry+i*stepX:ry-stepY-i*stepX;a=vec2(x,y);b=a+vec2(0,stepY);tint=shadeColor(increments,shade);
}
)GRID";
inline std::string gridVertexSource(bool bake = false) {
    auto source = std::string(R"GRID(#version 300 es
precision highp float;precision highp int;
uniform vec4 geometry[32];uniform vec4 lattice[32];uniform vec4 shading[32];uniform ivec4 info[32];
uniform int planeCount;uniform vec2 viewportSize;uniform vec2 pixelScale;uniform float beamWidth;uniform float intensity;
out vec2 strokeLocal;out vec2 strokeBeam;out vec4 strokeTint;flat out vec4 strokeRamp;flat out vec4 beamStyle;

)GRID") + gridGeometry +
                  R"GRID(
void main(){
 int plane=0;for(int i=0;i<32;++i){if(i>=planeCount)break;if(gl_InstanceID>=info[i].x&&gl_InstanceID<info[i].x+info[i].z){plane=i;break;}}
 vec4 geo=geometry[plane],lat=lattice[plane],ink=shading[plane];ivec4 desc=info[plane];
 vec4 gridShape=vec4(geo.zw,lat.w,ink.w);vec3 gridShade=ink.xyz;int gridPattern=desc.w;
 int count=primitiveCount(gridShape,gridPattern),local=gl_InstanceID-desc.x;
 int cell=local/count,gridId=local%count;
 vec2 index=vec2(float(cell/desc.y)+lat.y,float(cell%desc.y)+lat.z);
 vec2 center=(geo.xy+index*lat.x+vec2(.5))*pixelScale;
 float spread=gridPattern==1?3.0:4.0,reach=min(64.0,max(2.0,beamWidth*spread*2.0));
 vec2 a=vec2(0),b=vec2(0);vec2 low,high;vec3 tint=vec3(1);float energy=1.;bool ramp=false;
 primitive(gridId,gridShape,gridShade,gridPattern,a,b,tint,energy,ramp);a*=pixelScale;b*=pixelScale;low=min(a,b)-vec2(reach);high=max(a,b)+vec2(reach);
 vec2 corner=vec2(gl_VertexID==1||gl_VertexID==2||gl_VertexID==4?1.:0.,gl_VertexID==2||gl_VertexID==4||gl_VertexID==5?1.:0.);
 vec2 gridLocal=mix(low,high,corner);vec2 p=center+gridLocal;
 vec2 delta=b-a;float len=length(delta);vec2 unit=len>1.e-6?delta/len:vec2(1,0),relative=gridLocal-a;
 strokeLocal=vec2(dot(relative,unit),dot(relative,vec2(-unit.y,unit.x)));strokeBeam=vec2(len,beamWidth);strokeTint=vec4(tint,energy);
 strokeRamp=vec4(ink.xyz,ramp?-(ink.w+32.):0.);beamStyle=vec4(intensity,gridPattern==1?.42:.38,spread,0);
 gl_Position=max(tint.r,max(tint.g,tint.b))*energy>0.?vec4(p*vec2(2.,-2.)/viewportSize+vec2(-1.,1.),0,1):vec4(2,2,2,1);
}
)GRID";
    if (bake) {
        source.insert(source.find("uniform vec4 geometry"), "uniform float bakeScale;\n");
        const std::string reach = "min(64.0,max(2.0,beamWidth*spread*2.0))";
        source.replace(source.find(reach), reach.size(),
                       "min(64.0*bakeScale,max(2.0*bakeScale,beamWidth*spread*2.0))");
        const auto pos = source.find(" strokeRamp=");
        source.insert(pos, " strokeLocal/=bakeScale;strokeBeam/=bakeScale;\n");
    }
    return source;
}
} // namespace grpp
