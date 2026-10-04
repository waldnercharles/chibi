layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_normal;
layout(location=0) out vec3 v_world;
layout(location=1) out vec3 v_normal;
layout(location=2) in vec4 in_model0;
layout(location=2) flat out vec4 v_model0;
layout(location=3) in vec4 in_model1;
layout(location=3) flat out vec4 v_model1;
layout(location=4) in vec4 in_model2;
layout(location=4) flat out vec4 v_model2;
layout(location=5) in vec4 in_inverse0;
layout(location=5) flat out vec4 v_inverse0;
layout(location=6) in vec4 in_inverse1;
layout(location=6) flat out vec4 v_inverse1;
layout(location=7) in vec4 in_inverse2;
layout(location=7) flat out vec4 v_inverse2;
layout(location=8) in vec4 in_dimensions;
layout(location=8) flat out vec4 v_dimensions;
layout(location=9) in vec4 in_color0;
layout(location=9) flat out vec4 v_color0;
layout(location=10) in vec4 in_color1;
layout(location=10) flat out vec4 v_color1;
layout(location=11) in vec4 in_color2;
layout(location=11) flat out vec4 v_color2;
layout(location=12) in vec4 in_color3;
layout(location=12) flat out vec4 v_color3;
layout(location=13) in vec4 in_surface;
layout(location=13) flat out vec4 v_surface;
layout(location=14) in vec4 in_center;
layout(location=14) flat out vec4 v_center;
layout(set=1,binding=0) uniform uniform_block {
    vec4 u_rect;
    vec2 u_depth;
    vec4 u_eye;
    vec4 u_camera[4];
};
vec3 camera_point(vec3 world)
{
    if(u_camera[3].w<0.5) return world;
    vec3 target=vec3(u_camera[0].w,u_camera[1].w,u_camera[2].w);
    vec3 delta=world-target;
    return vec3(target.x+dot(delta,u_camera[0].xyz),dot(delta,u_camera[1].xyz),
        target.z+dot(delta,u_camera[2].xyz));
}
void main()
{
    v_model0=in_model0;
    v_model1=in_model1;
    v_model2=in_model2;
    v_inverse0=in_inverse0;
    v_inverse1=in_inverse1;
    v_inverse2=in_inverse2;
    v_dimensions=in_dimensions;
    v_color0=in_color0;
    v_color1=in_color1;
    v_color2=in_color2;
    v_color3=in_color3;
    v_surface=in_surface;
    v_center=in_center;
    vec4 p=vec4(in_pos,1);
    v_world=vec3(dot(in_model0,p),dot(in_model1,p),dot(in_model2,p));
    vec3 a=vec3(in_model0.x,in_model1.x,in_model2.x);
    vec3 b=vec3(in_model0.y,in_model1.y,in_model2.y);
    vec3 c=vec3(in_model0.z,in_model1.z,in_model2.z);
    v_normal=normalize(cross(b,c)*in_normal.x+cross(c,a)*in_normal.y+cross(a,b)*in_normal.z);
    vec3 aligned=camera_point(v_world);
    gl_Position=vec4((aligned.x-u_rect.x)/u_rect.z*2.0-1.0,
        (-aligned.z-u_rect.y)/u_rect.w*2.0+1.0,
        (u_depth.y-aligned.y)/(u_depth.y-u_depth.x),1);
    if (u_eye.w > 0.5) {
        float focal=u_camera[3].x;
        float d=focal-aligned.y, w=d/focal;
        gl_Position=vec4(
            2.0*(aligned.x-u_camera[0].w)/u_rect.z
                +(2.0*(u_camera[0].w-u_rect.x)/u_rect.z-1.0)*w,
            2.0*(-aligned.z+u_camera[2].w)/u_rect.w
                +(2.0*(-u_camera[2].w-u_rect.y)/u_rect.w+1.0)*w,
            u_depth.y/(u_depth.y-u_depth.x)*(d-u_depth.x)/focal,w);
    }
}
