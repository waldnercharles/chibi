layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_normal;
layout(location=2) in vec2 in_uv;
layout(location=0) out vec3 v_world;
layout(location=1) out vec3 v_normal;
layout(location=2) out vec3 v_local;
layout(location=3) out vec3 v_object_normal;
layout(location=4) out vec2 v_uv;
layout(set=1,binding=0) uniform uniform_block {
    vec4 u_model[3];
    vec4 u_rect;
    vec2 u_depth;
    vec4 u_eye;
};
void main()
{
    vec4 p=vec4(in_pos,1);
    v_world=vec3(dot(u_model[0],p),dot(u_model[1],p),dot(u_model[2],p));
    vec3 a=vec3(u_model[0].x,u_model[1].x,u_model[2].x);
    vec3 b=vec3(u_model[0].y,u_model[1].y,u_model[2].y);
    vec3 c=vec3(u_model[0].z,u_model[1].z,u_model[2].z);
    v_normal=normalize(cross(b,c)*in_normal.x+cross(c,a)*in_normal.y+cross(a,b)*in_normal.z);
    v_uv=in_uv;
    v_local=in_pos;
    v_object_normal=in_normal;
    gl_Position=vec4((v_world.x-u_rect.x)/u_rect.z*2.0-1.0,
        (-v_world.z-u_rect.y)/u_rect.w*2.0+1.0,
        (u_depth.y-v_world.y)/(u_depth.y-u_depth.x),1);
    if (u_eye.w > 0.5) {
        float d=u_eye.y-v_world.y, w=d/u_eye.y;
        gl_Position=vec4(
            2.0*(v_world.x-u_eye.x)/u_rect.z
                +(2.0*(u_eye.x-u_rect.x)/u_rect.z-1.0)*w,
            2.0*(-v_world.z+u_eye.z)/u_rect.w
                +(2.0*(-u_eye.z-u_rect.y)/u_rect.w+1.0)*w,
            u_depth.y/(u_depth.y-u_depth.x)*(d-u_depth.x)/u_eye.y,w);
    }
}
