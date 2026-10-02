layout(location=0) in vec3 v_world;
layout(location=1) in vec3 v_normal;
layout(location=2) in vec3 v_local;
layout(location=3) in vec3 v_object_normal;
layout(location=4) in vec2 v_uv;
layout(location=0) out vec4 result_scene;
layout(location=1) out vec4 result_emission;
layout(location=2) out vec4 result_absorption;
layout(location=3) out vec4 result_albedo;
layout(location=4) out vec4 result_normal;
vec3 finite_rgb(vec3 value)
{
    if (isnan(value.x)||isinf(value.x)) value.x=0.0;
    if (isnan(value.y)||isinf(value.y)) value.y=0.0;
    if (isnan(value.z)||isinf(value.z)) value.z=0.0;
    return clamp(value,vec3(0),vec3(65504));
}
void main()
{
    // Shared flat primitives carry both windings. The perspective eye also
    // sees side faces; reject only surfaces facing away from that eye.
    vec3 eye=u_eye.w>0.5 ? u_eye.xyz-v_world : vec3(0,1,0);
    if (dot(v_normal,eye) <= 0.0) discard;
    vec3 normal=normalize(v_normal);
    vec3 base=u_fallback[0].rgb, emission=u_fallback[1].rgb;
    vec3 albedo=u_fallback[2].rgb, absorption=u_fallback[3].rgb;
    float opacity=u_fallback[0].a, absorption_cover=u_fallback[3].a;
    float gloss=0.5, height=v_world.y;
    bool unlit=false;
    if (u_surface.x>0.0) {
        SGSample context;
        context.position=(v_local*u_dimensions.xyz).xz;
        vec3 side=abs(v_object_normal);
        context.uv=side.y>=side.x&&side.y>=side.z ? v_local.xz+0.5
            : side.x>=side.z ? vec2(v_local.z+0.5,0.5-v_local.y)
            : vec2(v_local.x+0.5,0.5-v_local.y);
        if (u_surface.w>0.5) context.uv=v_uv;
        context.object_position=v_local*u_dimensions.xyz;
        context.world_position=v_world;
        context.normal=normal;
        context.view_direction=normalize(eye);
        context.time=u_surface.y;
        vec4 parameters[32], surface[8];
        for (int i=0; i<32; i++) parameters[i]=u_parameters[i];
        sg_dispatch(int(u_surface.x),context,parameters,surface);
        base=surface[0].rgb; emission=surface[1].rgb;
        opacity=clamp(surface[5].x,0.0,1.0);
        unlit=surface[6].x>0.5;
        albedo=unlit?vec3(0):base*(1.0-clamp(surface[3].x,0.0,1.0));
        absorption=vec3(opacity); absorption_cover=opacity;
        gloss=1.0-clamp(surface[2].x,0.0,1.0);
        float n2=dot(surface[4].xyz,surface[4].xyz);
        if (n2>1e-8&&!isnan(n2)&&!isinf(n2)) normal=surface[4].xyz*inversesqrt(n2);
        if (!isnan(surface[7].x)&&!isinf(surface[7].x)) height+=surface[7].x;
    }
    // This first mesh contract is opaque. Zero opacity supports cutouts.
    if (isnan(opacity)||opacity<=0.0) discard;
    vec2 tilt=vec2(normal.x,-normal.z);
    float t2=dot(tilt,tilt);
    if(normal.y<=0.0&&t2>1e-8) tilt*=0.999*inversesqrt(t2);
    result_scene=vec4(finite_rgb(base),1);
    result_emission=vec4(finite_rgb(emission),1);
    result_absorption=vec4(finite_rgb(absorption),clamp(absorption_cover,0.0,1.0));
    result_albedo=vec4(finite_rgb(albedo),clamp(height,-65504.0,65504.0));
    result_normal=unlit?vec4(0):vec4(tilt,u_surface.z,gloss);
}
