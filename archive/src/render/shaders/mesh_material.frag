layout(location=0) in vec3 v_world;
layout(location=1) in vec3 v_normal;
layout(location=2) in vec3 v_local;
layout(location=3) in vec3 v_object_normal;
layout(location=4) in vec2 v_uv;
layout(location=5) in vec3 v_part_position;
layout(location=6) in vec3 v_part_frame0;
layout(location=7) in vec3 v_part_frame1;
layout(location=8) in vec3 v_part_frame2;
void main()
{
    if(mod(u_surface.w,4.0)<=1.5) {
        shade_surface(v_world,v_normal,v_local,v_object_normal,v_uv);
        return;
    }
    vec3 object_rows[3],part_rows[3],uv_rows[2];
    surface_object_frame(object_rows);
    vec3 part_object[3]=vec3[3](v_part_frame0,v_part_frame1,v_part_frame2);
    for(int i=0;i<3;i++) {
        part_rows[i]=part_object[i].x*object_rows[0]
            +part_object[i].y*object_rows[1]+part_object[i].z*object_rows[2];
    }
    vec2 uv=v_uv;
    if(u_uv_bounds[0].w>0.5) {
        vec3 extent=u_uv_bounds[1].xyz-u_uv_bounds[0].xyz;
        vec3 center=(u_uv_bounds[0].xyz+u_uv_bounds[1].xyz)*0.5;
        vec3 local=(v_local-center)/extent;
        vec3 side=abs(v_object_normal);
        uv=side.y>=side.x&&side.y>=side.z ? local.xz+0.5
            : side.x>=side.z ? vec2(local.z+0.5,0.5-local.y)
            : vec2(local.x+0.5,0.5-local.y);
    }
    surface_uv_frame(v_world,uv,uv_rows);
    vec4 parameters[32];
    for(int i=0;i<32;i++) parameters[i]=u_parameters[i];
    shade_surface_points(v_world,v_normal,v_local,v_part_position,uv,
        object_rows,part_rows,uv_rows,int(u_surface.x),parameters);
}
