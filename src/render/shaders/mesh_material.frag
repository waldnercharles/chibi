layout(location=0) in vec3 v_world;
layout(location=1) in vec3 v_normal;
layout(location=2) in vec3 v_local;
layout(location=3) in vec3 v_object_normal;
layout(location=4) in vec2 v_uv;
void main()
{
    shade_surface(v_world,v_normal,v_local,v_object_normal,v_uv);
}
