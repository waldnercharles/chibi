// Direct spot lighting. Traverse every crossed texel in the known canvas,
// including thin mask walls. No temporal feedback.
layout(set=2,binding=0) uniform sampler2D u_albedo;
layout(set=2,binding=1) uniform sampler2D u_cover;
layout(set=2,binding=2) uniform sampler2D u_normal;
layout(set=2,binding=3) uniform sampler2D u_source;
layout(location=0) out vec4 result;
layout(set=3,binding=0) uniform uniform_block {
    vec4 u_positions[4];
    vec4 u_colors[4];
    vec4 u_cones[4];
    vec4 u_options[4];
    vec4 u_projection;
    vec2 u_spacing;
    float u_thickness;
    float u_specular;
    float u_density;
    int u_count;
};
#include "hrc/brdf.shd"
#include "hrc/surface_geometry.shd"

float ray_height(float t, float receiver, float emitter)
{
    if(u_projection.z<=0.0) return mix(receiver,emitter,t);
    float a=1.0/max(u_projection.z-receiver,0.001);
    float b=1.0/max(u_projection.z-emitter,0.001);
    return u_projection.z-1.0/mix(a,b,t);
}

float visibility(vec2 begin, vec2 end, float receiver, float emitter, bool skip_receiver)
{
    ivec2 size=textureSize(u_cover,0);
    vec2 delta=end-begin;
    vec2 direction=sign(delta);
    vec2 reciprocal=1.0/max(abs(delta),vec2(1e-20));
    // Clip both endpoints to the known canvas, preserving the original t
    // used for perspective height. Near-plane clipping can move the start
    // of a volume sample's shadow ray outside the canvas.
    float entered=0.0, end_t=1.0;
    for(int axis=0;axis<2;axis++) {
        if(direction[axis]==0.0) {
            if(begin[axis]<0.0 || begin[axis]>=float(size[axis])) return 1.0;
        } else {
            float a=-begin[axis]/delta[axis];
            float b=(float(size[axis])-begin[axis])/delta[axis];
            entered=max(entered,min(a,b));
            end_t=min(end_t,max(a,b));
        }
    }
    if(entered>end_t) return 1.0;
    ivec2 cell=ivec2(floor(clamp(begin+delta*entered,vec2(0),vec2(size)-vec2(0.001))));
    vec2 edge=vec2(cell)+max(direction,vec2(0));
    vec2 next=max((edge-begin)*direction*reciprocal,vec2(entered));
    if(direction.x==0.0) next.x=1e20;
    if(direction.y==0.0) next.y=1e20;
    float transmission=1.0;
    // Each iteration advances one axis. A clipped segment crosses at most
    // width+height boundaries, including a zero-length corner crossing.
    int steps=size.x+size.y+1;
    for(int i=0;i<steps;i++) {
        float leave=min(min(next.x,next.y),end_t);
        if(all(greaterThanEqual(cell,ivec2(0))) && all(lessThan(cell,size))) {
            float cover=texelFetch(u_cover,cell,0).g;
            vec4 source=texelFetch(u_source,cell,0);
            float tau=max(source.a,0.0);
            if(cover>1e-6) {
                float top=texelFetch(u_albedo,cell,0).a/cover;
                float a=ray_height(entered,receiver,emitter);
                float b=ray_height(leave,receiver,emitter);
                float bias=max(max(abs(top),abs(receiver))*0.001,0.002);
                // Same Depth contract as surface_depth.frag: nonabsorbing
                // nonemissive geometry still casts depth shadows. A light
                // that absorbs nothing does not occlude another light.
                bool occluder=tau>0.01 || dot(source.rgb,vec3(.2126,.7152,.0722))<=0.0;
                if(occluder && min(a,b)<top-bias && max(a,b)>top-u_thickness+bias) return 0.0;
            } else if(!skip_receiver || entered>0.0) {
                // Exclude the surface receiver's own planar absorption only.
                // Slabs use their depth bias, including at the emitter cell.
                transmission*=exp(-tau*length(delta)*(leave-entered));
                if(transmission<0.001) return 0.0;
            }
        }
        if(leave>=end_t) return transmission;
        entered=leave;
        if(next.x<next.y) { cell.x+=int(direction.x); next.x+=reciprocal.x; }
        else { cell.y+=int(direction.y); next.y+=reciprocal.y; }
    }
    return 0.0;
}

// Keep the visible portion of the actual 3D segment before projecting.
// RENDER_MESH_NEAR is one world unit: no known surface lies closer to the eye.
float light_visibility(vec2 pixel, vec3 receiver, vec3 emitter, bool surface_receiver)
{
    if(u_projection.z>0.0) {
        float near_height=u_projection.z-1.0;
        if(receiver.z>near_height && emitter.z>near_height) return 1.0;
        if(receiver.z>near_height) {
            receiver=mix(receiver,emitter,(near_height-receiver.z)/(emitter.z-receiver.z));
            pixel=(receiver.xy/(1.0-receiver.z/u_projection.z)-u_projection.xy)/(u_spacing*vec2(1,-1));
            surface_receiver=false;
        }
        if(emitter.z>near_height) {
            emitter=mix(receiver,emitter,(near_height-receiver.z)/(emitter.z-receiver.z));
        }
    }
    float scale=u_projection.z>0.0?1.0-emitter.z/u_projection.z:1.0;
    vec2 end=(emitter.xy/scale-u_projection.xy)/(u_spacing*vec2(1,-1));
    return visibility(pixel,end,receiver.z,emitter.z,surface_receiver);
}

// Intersect the forward cone as well as the sphere before quadrature, so a
// narrow cone cannot fall entirely between the eight depth samples.
bool cone_interval(vec3 relative,vec3 ray,vec4 cone,inout float first,inout float last)
{
    float axial=dot(relative,cone.xyz), slope=dot(ray,cone.xyz);
    if(abs(slope)<1e-7) {
        if(axial<0.0) return false;
    } else if(slope>0.0) first=max(first,-axial/slope);
    else last=min(last,-axial/slope);
    float cosine2=cone.w*cone.w;
    float a=slope*slope-cosine2;
    float b=2.0*(axial*slope-cosine2*dot(relative,ray));
    float c=axial*axial-cosine2*dot(relative,relative);
    if(abs(a)<1e-7) {
        if(abs(b)<1e-7) return c>=0.0 && last>first;
        if(b>0.0) first=max(first,-c/b);
        else last=min(last,-c/b);
        return last>first;
    }
    float discriminant=b*b-4.0*a*c;
    if(discriminant<0.0) return a>0.0 && last>first;
    float root=sqrt(discriminant);
    float x=(-b-root)/(2.0*a), y=(-b+root)/(2.0*a);
    float lower=min(x,y), upper=max(x,y);
    if(a<0.0) {
        first=max(first,lower);
        last=min(last,upper);
    } else if(first<lower) last=min(last,lower);
    else first=max(first,upper);
    return last>first;
}

// Homogeneous, isotropic single scattering through each source's sphere.
// All lights share the same inverse-world-unit density. Only the known front
// layer supplies shadows; opaque coverage clips samples behind that layer.
vec3 volume_light(vec2 pixel, float height, float coverage)
{
    if(u_density<=0.0) return vec3(0);
    vec2 plane=u_projection.xy+pixel*u_spacing*vec2(1,-1);
    vec3 sum=vec3(0);
    for(int i=0;i<4;i++) {
        if(i>=u_count) break;
        vec3 source=u_positions[i].xyz;
        float radius=u_positions[i].w;
        vec3 origin=vec3(plane,source.z+radius);
        vec3 ray=vec3(0,0,-1);
        if(u_projection.z>0.0) {
            origin=vec3(0,0,u_projection.z);
            ray=normalize(vec3(plane,-u_projection.z));
        }
        vec3 relative=origin-source;
        float b=dot(relative,ray);
        float discriminant=b*b-dot(relative,relative)+radius*radius;
        if(discriminant<=0.0) continue;
        float root=sqrt(discriminant);
        float first=max(-b-root,0.0), last=-b+root;
        if(coverage>1e-6) last=min(last,(height-origin.z)/ray.z);
        if(last<=first) continue;
        float volume_entry=first;
        if(!cone_interval(relative,ray,u_cones[i],first,last)) continue;
        float step=(last-first)/8.0;
        for(int j=0;j<8;j++) {
            float along=first+(float(j)+0.5)*step;
            vec3 point=origin+ray*along;
            vec3 delta=source-point;
            float distance=length(delta);
            if(distance<=0.0001 || distance>=radius) continue;
            vec3 l=delta/distance;
            float cone=clamp((dot(-l,u_cones[i].xyz)-u_cones[i].w)/
                            max(u_options[i].x-u_cones[i].w,0.0001),0.0,1.0);
            cone=cone*cone*(3.0-2.0*cone);
            if(cone<=0.0) continue;
            float visible=1.0;
            if(u_options[i].y>0.5) {
                visible=light_visibility(pixel,point,source,false);
            }
            float attenuation=pow(max(1.0-distance/radius,0.0),2.0);
            // Beer attenuation on source→sample and the view's participating
            // segment. The external camera-to-sphere medium is unmodelled.
            float transmission=exp(-u_density*(distance+along-volume_entry));
            sum+=u_colors[i].rgb*u_colors[i].a*cone*attenuation*visible*
                 transmission*u_density*step*(1.0/12.566370614359);
        }
    }
    return sum;
}

void main()
{
    ivec2 p=ivec2(gl_FragCoord.xy);
    vec4 cover=texelFetch(u_cover,p,0);
    result=vec4(0);
    if(cover.g<=1e-6) {
        result.rgb=min(volume_light(vec2(p)+0.5,0.0,0.0),vec3(65504));
        return;
    }
    vec4 normal=texelFetch(u_normal,p,0)/cover.g;
    if(normal.z<0.0) return;
    vec3 n=normalize(vec3(normal.xy,sqrt(max(1.0-dot(normal.xy,normal.xy),0.0))));
    vec4 albedo=texelFetch(u_albedo,p,0);
    float height=albedo.a/cover.g;
    vec3 origin=surface_position(vec2(p)+0.5,height,u_projection,u_spacing);
    for(int i=0;i<4;i++) {
        if(i>=u_count) break;
        vec3 delta=u_positions[i].xyz-origin;
        float distance=length(delta);
        if(distance<=0.0001 || distance>=u_positions[i].w) continue;
        vec3 l=delta/distance;
        float cone=clamp((dot(-l,u_cones[i].xyz)-u_cones[i].w)/
                         max(u_options[i].x-u_cones[i].w,0.0001),0.0,1.0);
        cone=cone*cone*(3.0-2.0*cone);
        float diffuse=max(dot(n,l),0.0);
        if(cone<=0.0 || diffuse<=0.0) continue;
        float visible=1.0;
        if(u_options[i].y>0.5) {
            visible=light_visibility(vec2(p)+0.5,origin,u_positions[i].xyz,true);
        }
        float attenuation=pow(max(1.0-distance/u_positions[i].w,0.0),2.0);
        vec3 incident=u_colors[i].rgb*u_colors[i].a*cone*attenuation*visible;
        float gloss=clamp(normal.w,0.0,1.0);
        float specular=gloss>0.0?highlight(n,l,1.0-gloss)*u_specular:0.0;
        result.rgb+=incident*(albedo.rgb*diffuse+vec3(specular*cover.g))*clamp(normal.z,0.0,1.0);
    }
    result.rgb+=volume_light(vec2(p)+0.5,height,cover.g);
    result.rgb=min(result.rgb,vec3(65504));
}
