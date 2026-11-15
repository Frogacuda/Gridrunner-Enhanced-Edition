#pragma once
// Adapted from the Neon vector renderer; bounded phosphor accumulation/resolve.
namespace grpp {
inline const char *beamVertexSource = R"NEON(#version 300 es
precision highp float;
layout(location=0) in vec2 position;
layout(location=1) in vec2 localPosition;
layout(location=2) in vec2 beam;
layout(location=3) in vec4 tint;
layout(location=4) in vec4 ramp;
uniform vec2 viewportSize;
out vec2 strokeLocal;out vec2 strokeBeam;out vec4 strokeTint;flat out vec4 strokeRamp;
void main(){gl_Position=vec4(position*vec2(2.0,-2.0)/viewportSize+vec2(-1.0,1.0),0.0,1.0);strokeLocal=localPosition;strokeBeam=beam;strokeTint=tint;strokeRamp=ramp;}
)NEON";
inline const char *beamSampleFragmentSource = R"NEON(#version 300 es
precision highp float;
in vec2 strokeLocal;in vec2 strokeBeam;in vec4 strokeTint;
uniform vec4 beamStyle; // intensity, halo, spread, white-hot
uniform vec4 phosphorResponse; // point boost, intersection boost, whitening limit, encoding scale
uniform int roundCull;
layout(location=0) out vec4 colour;layout(location=1) out vec4 coverage;
void main(){
 float endpoint=max(max(-strokeLocal.x,strokeLocal.x-strokeBeam.x),0.0);
 float d2=endpoint*endpoint+strokeLocal.y*strokeLocal.y;
 float reach=min(64.0,max(2.0,strokeBeam.y*beamStyle.z*2.0));
 if(roundCull!=0&&d2>=reach*reach)discard;
 float w=strokeBeam.y,aa2=w*w+0.46209812;
 float core=exp2(-4.0*d2/aa2)*w*inversesqrt(aa2);
 float halo2=w*w*beamStyle.z*beamStyle.z;
 float nearHalo=0.0,farHalo=0.0;if(beamStyle.y>0.0){nearHalo=exp2(-4.0*d2/halo2);farHalo=exp2(-4.0*d2/(halo2*5.29));float reach=min(64.0,max(2.0,w*beamStyle.z*2.0));float fade=clamp((reach*reach-d2)/(reach*reach*0.4375),0.0,1.0);fade=fade*fade*(3.0-2.0*fade);nearHalo*=fade;farHalo*=fade;}
 vec3 hot=mix(strokeTint.rgb,vec3(1.0),clamp(beamStyle.w,0.0,1.0)*core);
 vec3 light=(hot*core+strokeTint.rgb*beamStyle.y*(0.22*nearHalo+0.018*farHalo))*strokeTint.a*beamStyle.x;
 if(strokeBeam.x<0.00001)light*=phosphorResponse.x;
 // Logarithmic RGB10_A2 accumulation avoids a linear radiance ceiling.
 // Screen blending adds the decoded light; R8 tracks smooth core exposures.
 colour=vec4(vec3(1)-exp(-light*phosphorResponse.w),0);
 coverage=vec4(1.0-exp(-exp2(-4.0*d2/aa2)*.5),0,0,0);
}
)NEON";
inline const char *beamFragmentSource = R"NEON(#version 300 es
precision highp float;
in vec2 strokeLocal;in vec2 strokeBeam;in vec4 strokeTint;flat in vec4 strokeRamp;
uniform vec4 beamStyle; // intensity, halo, spread, white-hot
uniform vec4 phosphorResponse; // point boost, intersection boost, whitening limit, encoding scale
uniform int roundCull;
layout(location=0) out vec4 colour;layout(location=1) out vec4 coverage;
void main(){
 float endpoint=max(max(-strokeLocal.x,strokeLocal.x-strokeBeam.x),0.0);
 float d2=endpoint*endpoint+strokeLocal.y*strokeLocal.y;
 float reach=min(64.0,max(2.0,strokeBeam.y*beamStyle.z*2.0));
 if(roundCull!=0&&d2>=reach*reach)discard;
 if(strokeRamp.w<0.0){
    // Artistic alternative: one continuous gradient, one cap/halo footprint.
    float n=-strokeRamp.w;bool compensated=n>32.0;if(compensated)n-=32.0;float t=clamp(strokeLocal.x/max(strokeBeam.x,.00001),0.0,1.0);
    vec3 tint=clamp(strokeRamp.rgb*(1.0+t*(n-1.0))/16711680.0,0.0,1.0);
    float w=strokeBeam.y,aa2=w*w+0.46209812,gaussian=exp2(-4.0*d2/aa2);
    float core=gaussian*w*inversesqrt(aa2),halo2=w*w*beamStyle.z*beamStyle.z;
    float fade=clamp((reach*reach-d2)/(reach*reach*0.4375),0.0,1.0);fade=fade*fade*(3.0-2.0*fade);
    float halo=(0.22*exp2(-4.0*d2/halo2)+0.018*exp2(-4.0*d2/(halo2*5.29)))*fade;
    vec3 light=tint*(core+beamStyle.y*halo)*strokeTint.a*beamStyle.x;
    float exposure=gaussian*.5;
    if(compensated){
        // A segment's longitudinal kernel has area L + A, where A is
        // the combined Gaussian cap area. N source segments have L + N*A.
        // Match this energy separately for core, near halo and far halo.
        // The smooth rational gain approaches N for tiny projected ramps
        // and 1 for long ramps; it needs no brightness ceiling.
        vec3 capArea=1.06446702*sqrt(vec3(aa2,halo2,halo2*5.29));
        vec3 gain=(vec3(strokeBeam.x)+n*capArea)/(vec3(strokeBeam.x)+capArea);
        float near=exp2(-4.0*d2/halo2),far=exp2(-4.0*d2/(halo2*5.29));
        light=tint*(core*gain.x+beamStyle.y*fade*(.22*near*gain.y+.018*far*gain.z))*strokeTint.a*beamStyle.x;
        exposure=gaussian*.5*gain.x;
    }
    colour=vec4(vec3(1)-exp(-light*phosphorResponse.w),0);
    coverage=vec4(1.0-exp(-exposure),0,0,0);return;
 }
 if(strokeRamp.w>0.0){
    // One ramp primitive replaces N independently capped source segments.
    // Recover source RGB565 shades exactly, including fixed-point increments.
    float n=strokeRamp.w,stepLength=strokeBeam.x/n,w=strokeBeam.y,aa2=w*w+0.46209812;
    float halo2=w*w*beamStyle.z*beamStyle.z,totalExposure=0.0;vec3 totalLight=vec3(0);
    int first=max(0,int(floor((strokeLocal.x-reach)/stepLength)));
    int last=min(int(n)-1,int(floor((strokeLocal.x+reach)/stepLength)));
    for(int i=first;i<=last;++i){
        float start=float(i)*stepLength,finish=start+stepLength;
        float cap=max(max(start-strokeLocal.x,strokeLocal.x-finish),0.0),sd2=cap*cap+strokeLocal.y*strokeLocal.y;
        if(sd2>=reach*reach)continue;
        uvec3 q=uvec3(strokeRamp.rgb*float(i+1))>>16;uvec3 bits=q/uvec3(8,4,8);
        vec3 tint=vec3((bits<<uvec3(3,2,3))|(bits>>uvec3(2,4,2)))/255.0;
        if(max(tint.r,max(tint.g,tint.b))<=0.0)continue;
        float gaussian=exp2(-4.0*sd2/aa2),core=gaussian*w*inversesqrt(aa2);
        float fade=clamp((reach*reach-sd2)/(reach*reach*0.4375),0.0,1.0);fade=fade*fade*(3.0-2.0*fade);
        float halo=(0.22*exp2(-4.0*sd2/halo2)+0.018*exp2(-4.0*sd2/(halo2*5.29)))*fade;
        totalLight+=tint*(core+beamStyle.y*halo)*strokeTint.a*beamStyle.x;
        totalExposure+=gaussian*.5;
    }
    colour=vec4(vec3(1)-exp(-totalLight*phosphorResponse.w),0);
    coverage=vec4(1.0-exp(-totalExposure),0,0,0);return;
 }
 float w=strokeBeam.y,aa2=w*w+0.46209812;
 float core=exp2(-4.0*d2/aa2)*w*inversesqrt(aa2);
 float halo2=w*w*beamStyle.z*beamStyle.z;
 float nearHalo=0.0,farHalo=0.0;if(beamStyle.y>0.0){nearHalo=exp2(-4.0*d2/halo2);farHalo=exp2(-4.0*d2/(halo2*5.29));float reach=min(64.0,max(2.0,w*beamStyle.z*2.0));float fade=clamp((reach*reach-d2)/(reach*reach*0.4375),0.0,1.0);fade=fade*fade*(3.0-2.0*fade);nearHalo*=fade;farHalo*=fade;}
 vec3 hot=mix(strokeTint.rgb,vec3(1.0),clamp(beamStyle.w,0.0,1.0)*core);
 vec3 light=(hot*core+strokeTint.rgb*beamStyle.y*(0.22*nearHalo+0.018*farHalo))*strokeTint.a*beamStyle.x;
 if(strokeBeam.x<0.00001)light*=phosphorResponse.x;
 // Logarithmic RGB10_A2 accumulation avoids a linear radiance ceiling.
 // Screen blending adds the decoded light; R8 tracks smooth core exposures.
 colour=vec4(vec3(1)-exp(-light*phosphorResponse.w),0);
 coverage=vec4(1.0-exp(-exp2(-4.0*d2/aa2)*.5),0,0,0);
}
)NEON";
inline const char *beamResolveVertexSource = R"NEON(#version 300 es
precision highp float;
uniform int orientation;out vec2 uv;
void main(){vec2 p=vec2(gl_VertexID==1?3.0:-1.0,gl_VertexID==2?3.0:-1.0);vec2 q=p*.5+.5;uv=orientation==1?vec2(1.-q.y,q.x):orientation==2?vec2(q.y,1.-q.x):orientation==3?vec2(1.)-q:q;gl_Position=vec4(p,0,1);}
)NEON";
inline const char *beamResolveFragmentSource = R"NEON(#version 300 es
precision highp float;
in vec2 uv;uniform sampler2D radiance;uniform sampler2D exposure;uniform vec4 phosphorResponse;out vec4 colour;
void main(){vec4 accumulated=texture(radiance,uv);
 float coreExposure=-2.0*log(max(1.0-texture(exposure,uv).r,1.0/65536.0));
 float join=smoothstep(1.0,2.0,coreExposure);
 vec3 light=(-log(max(vec3(1)-accumulated.rgb,vec3(1.0/65536.0)))/phosphorResponse.w)*(1.0+(phosphorResponse.y-1.0)*join);
 float peak=max(light.r,max(light.g,light.b));
 vec3 base=light/max(1.0,peak);
 float whitening=phosphorResponse.z*(1.0-exp(-max(peak-1.0,0.0)/max(phosphorResponse.z,.0001)));
 colour=vec4(mix(base,vec3(1),whitening),1);
}
)NEON";
} // namespace grpp
