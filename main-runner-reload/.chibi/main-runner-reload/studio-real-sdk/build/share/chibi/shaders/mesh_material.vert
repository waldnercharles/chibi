layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_normal;
layout(location=2) in vec2 in_uv;
layout(location=3) in vec3 in_part_position;
layout(location=4) in vec3 in_part_frame0;
layout(location=5) in vec3 in_part_frame1;
layout(location=6) in vec3 in_part_frame2;
layout(location=0) out vec3 v_world;
layout(location=1) out vec3 v_normal;
layout(location=2) out vec3 v_local;
layout(location=3) out vec3 v_object_normal;
layout(location=4) out vec2 v_uv;
layout(location=5) out vec3 v_part_position;
layout(location=6) out vec3 v_part_frame0;
layout(location=7) out vec3 v_part_frame1;
layout(location=8) out vec3 v_part_frame2;
layout(set=1,binding=0) uniform uniform_block {
    vec4 u_model[3];
    vec4 u_rect;
    vec2 u_depth;
    vec4 u_eye;
    vec4 u_camera[4];
    float u_part_coordinates;
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
    vec4 p=vec4(in_pos,1);
    v_world=vec3(dot(u_model[0],p),dot(u_model[1],p),dot(u_model[2],p));
    vec3 a=vec3(u_model[0].x,u_model[1].x,u_model[2].x);
    vec3 b=vec3(u_model[0].y,u_model[1].y,u_model[2].y);
    vec3 c=vec3(u_model[0].z,u_model[1].z,u_model[2].z);
    v_normal=normalize(cross(b,c)*in_normal.x+cross(c,a)*in_normal.y+cross(a,b)*in_normal.z);
    v_uv=in_uv;
    bool authored=u_part_coordinates>0.5;
    v_part_position=authored?in_part_position:in_pos;
    v_part_frame0=authored?in_part_frame0:vec3(1,0,0);
    v_part_frame1=authored?in_part_frame1:vec3(0,1,0);
    v_part_frame2=authored?in_part_frame2:vec3(0,0,1);
    v_local=in_pos;
    v_object_normal=in_normal;
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
