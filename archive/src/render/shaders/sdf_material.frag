#ifndef SG_SHAPE_CALL
#define SG_SHAPE_CALL(id,context,values,result) sg_dispatch(id,context,values,result)
#endif
// Analytic shape graphs are distance bounds in authored object units.
// The box is only a raster proxy; hit depth participates in the mesh group's depth test.
layout(location=0) in vec3 v_world;
layout(location=1) in vec3 v_normal;

vec3 object_point(vec3 p, float w)
{
    vec4 v=vec4(p,w);
    return vec3(dot(v_inverse0,v),dot(v_inverse1,v),dot(v_inverse2,v))+v_center.xyz*w;
}
SGSample shape_context(vec3 p)
{
    SGSample context;
    context.position=p.xz;
    context.uv=p.xz/u_dimensions.xz+0.5;
    context.object_position=p;
    context.part_position=p;
    vec4 local=vec4((p-v_center.xyz)/u_dimensions.xyz,1);
    for(int i=0;i<3;i++) { context.object_from_world[i]=vec3(0); context.part_from_world[i]=vec3(0); }
    for(int i=0;i<2;i++) context.uv_from_world[i]=vec3(0);
    context.world_position=vec3(dot(v_model0,local),dot(v_model1,local),dot(v_model2,local));
    context.normal=vec3(0,1,0);
    context.view_direction=u_camera[3].w<0.5 ? vec3(0,1,0)
        : (u_eye.w>0.5 ? normalize(u_eye.xyz-context.world_position) : u_camera[1].xyz);
    context.time=u_surface.y;
    return context;
}
float distance_at(vec3 p)
{
    SGSample context=shape_context(p);
    vec4 values[32], result[SG_OUTPUTS];
    for(int i=0;i<32;i++) values[i]=SG_SHAPE_PARAMETER(i);
#ifdef SG_MODEL_ISOLATE
    sg_preview_distance(context,values,result);
    float distance=result[0].x*u_preview_options.x;
#else
    SG_SHAPE_CALL(int(u_shape.x),context,values,result);
    float distance=result[0].x;
#endif
#ifdef SG_MODEL_PREVIEW
    if(u_preview_options.y>0.5) distance=max(distance,dot(p,u_preview_plane.xyz)-u_preview_plane.w);
#endif
    return distance;
}
#if SG_ANALYTIC_GRADIENT
vec3 gradient_at(vec3 p, float tolerance)
{
    vec4 values[32];
    for(int i=0;i<32;i++) values[i]=SG_SHAPE_PARAMETER(i);
    return SG_GRADIENT_CALL(shape_context(p),values,tolerance);
}
#endif
void hidden_fragment(int evaluations, float reason)
{
    if(u_sdf_quality.z<1.5) discard;
    // Test-only readback includes discarded rays without GPU atomics.
    result_scene=vec4(float(evaluations)/128.0,1,reason,1);
    result_emission=vec4(0);
    result_absorption=vec4(0);
    result_albedo=vec4(0);
    result_normal=vec4(0);
    gl_FragDepth=0.0;
}
void main()
{
    vec3 aligned=camera_point(v_world);
    vec3 origin=u_eye.w>0.5 ? u_eye.xyz : camera_world(vec3(aligned.x,u_depth.y,aligned.z));
    vec3 direction=u_eye.w>0.5 ? normalize(v_world-origin) : -u_camera[1].xyz;
    float direction_up=camera_vector(direction).y;
    // Begin at the camera near plane so bounds crossing it can still expose later surfaces.
    if(u_eye.w>0.5) origin+=direction*(u_depth.x/max(-direction_up,1e-7));
    vec3 start=object_point(origin,1);
    vec3 ray=normalize(object_point(direction,0));
    // Derivatives must execute before divergent tracing or fragment rejection.
    // Their ray cone follows perspective depth and the instance inverse scale.
    vec3 start_dx=vec3(0), start_dy=vec3(0), ray_dx=vec3(0), ray_dy=vec3(0);
    if(u_sdf_quality.x>0.0) {
        start_dx=dFdx(start); start_dy=dFdy(start);
        ray_dx=dFdx(ray); ray_dy=dFdy(ray);
    }
    vec3 half_size=u_dimensions.xyz*0.5;
    // Outside the proxy only its entry faces need to trace. Inside, retain exit faces.
    if(any(greaterThan(abs(start-v_center.xyz),half_size))&&dot(v_normal,-direction)<=0.0) discard;
    float entry=0.0, exit_distance=1e30;
    for(int axis=0;axis<3;axis++) {
        if(abs(ray[axis])<1e-7) {
            if(abs(start[axis]-v_center[axis])>half_size[axis]) discard;
        } else {
            float a=(v_center[axis]-half_size[axis]-start[axis])/ray[axis];
            float b=(v_center[axis]+half_size[axis]-start[axis])/ray[axis];
            entry=max(entry,min(a,b));
            exit_distance=min(exit_distance,max(a,b));
        }
    }
    if(entry>exit_distance) discard;
    float visible_depth=1.0;
    bool clipped=false;
    if(u_occlusion.x>0.5) {
        visible_depth=min(1.0,texelFetch(u_geometric_depth,ivec2(gl_FragCoord.xy),0).r+u_occlusion.y);
        if(visible_depth<1.0) {
            float y=u_depth.y-visible_depth*(u_depth.y-u_depth.x);
            if(u_eye.w>0.5) {
                float d=u_depth.x*u_depth.y/(u_depth.y-visible_depth*(u_depth.y-u_depth.x));
                y=u_camera[3].x-d;
            }
            float limit=(y-camera_point(origin).y)/direction_up*length(object_point(direction,0));
            clipped=limit<exit_distance;
            exit_distance=min(exit_distance,limit);
            if(entry>exit_distance) { hidden_fragment(0,1.0); return; }
        }
    }
    float epsilon=max(0.00001,max(u_dimensions.x,max(u_dimensions.y,u_dimensions.z))*0.0002);
    float tolerance_cap=max(epsilon,max(u_dimensions.x,max(u_dimensions.y,u_dimensions.z))*0.002);
    float t=entry;
    int evaluations=0;
    bool hit=false, attempted_refinement=false;
    vec3 point=start+ray*t;
#if SG_ANALYTIC_SPHERE
    if(u_sdf_quality.y<0.5) {
        // Exact recognized sphere: one graph evaluation obtains its animated
        // radius. Intersect in object space, including camera-inside cases.
        float radius=-distance_at(vec3(0));
        evaluations++;
        if(radius<=0.0||isnan(radius)||isinf(radius)) discard;
        float b=dot(start,ray);
        vec3 closest=start-ray*b;
        float discriminant=radius*radius-dot(closest,closest);
        if(discriminant<0.0) discard;
        float root=sqrt(discriminant);
        float first=-b-root, last=-b+root;
        t=first>=entry ? first : last;
        if(t<entry) discard;
        if(t>exit_distance) {
            if(clipped) { hidden_fragment(evaluations,0.5); return; }
            discard;
        }
        point=start+ray*t;
        hit=true;
    }
#endif
#ifndef SG_DISTANCE_LIPSCHITZ
#define SG_DISTANCE_LIPSCHITZ 1.0
#endif
    for(int step=0;step<(SG_DISTANCE_LIPSCHITZ>1.0?512:128)&&!hit;step++) {
        point=start+ray*t;
        float distance=distance_at(point);
        evaluations++;
        if(isnan(distance)||isinf(distance)) discard;
        if(abs(distance)<=epsilon) { hit=true; break; }
        if(u_sdf_quality.x>0.0) {
            float footprint=max(length(start_dx+t*ray_dx),length(start_dy+t*ray_dy));
            float tolerance=clamp(u_sdf_quality.x*footprint,epsilon,tolerance_cap);
            // A coarse candidate is never a hit by itself. Refine against the
            // reference residual so silhouettes and thin gaps keep their contract.
            if(!attempted_refinement&&tolerance>epsilon&&abs(distance)<=tolerance) {
                attempted_refinement=true;
                float refined=t, residual=distance;
                for(int refine=0;refine<4;refine++) {
                    float probe=distance_at(start+ray*(refined+epsilon));
                    evaluations++;
                    float slope=(probe-residual)/epsilon;
                    // Near-tangent roots are ill conditioned: a small residual
                    // change can move depth/normals substantially. Keep the
                    // reference march for these rays instead of extrapolating.
                    if(abs(slope)<0.25||isnan(slope)||isinf(slope)) break;
                    float next=refined-residual/slope;
                    if(next<entry||next>exit_distance||abs(next-refined)>tolerance*4.0) break;
                    refined=next;
                    residual=distance_at(start+ray*refined);
                    evaluations++;
                    if(isnan(residual)||isinf(residual)) break;
                    if(abs(residual)<=epsilon) {
                        t=refined; point=start+ray*t; hit=true; break;
                    }
                }
                if(hit) break;
            }
        }
        t+=SG_DISTANCE_LIPSCHITZ>1.0 ? abs(distance)*0.8/SG_DISTANCE_LIPSCHITZ : max(abs(distance)*0.8,epsilon*0.25);
        if(t>exit_distance) break;
    }
    if(!hit) {
        if(clipped&&t>exit_distance) { hidden_fragment(evaluations,0.5); return; }
        discard;
    }
    vec3 local=(point-v_center.xyz)/u_dimensions.xyz;
    vec4 p=vec4(local,1);
    vec3 world=vec3(dot(v_model0,p),dot(v_model1,p),dot(v_model2,p));
    float camera_y=camera_point(world).y;
    float depth=(u_depth.y-camera_y)/(u_depth.y-u_depth.x);
    if(u_eye.w>0.5) {
        float d=u_camera[3].x-camera_y;
        if(d<u_depth.x) discard;
        depth=u_depth.y/(u_depth.y-u_depth.x)*(d-u_depth.x)/d;
    }
    if(depth<0.0||depth>1.0) discard;
    if(depth>visible_depth) { hidden_fragment(evaluations,0.5); return; }
    gl_FragDepth=depth;
    vec2 e=vec2(epsilon,0);
    vec3 normal=vec3(0);
    int normal_evaluations=0;
    bool analytic_normal=false;
#if SG_ANALYTIC_SPHERE
    if(u_sdf_quality.y<0.5) {
        normal=point;
        analytic_normal=true;
    }
#elif SG_ANALYTIC_GRADIENT
    if(u_sdf_quality.y<0.5) {
        normal=gradient_at(point,epsilon);
        normal_evaluations++;
        float squared=dot(normal,normal);
        analytic_normal=squared>=1e-16&&!isnan(squared)&&!isinf(squared);
    }
#endif
    if(!analytic_normal&&u_sdf_quality.y<5.0) {
        // The four tetrahedral offsets have zero mean and an isotropic moment.
        vec3 a=vec3(1,-1,-1), b=vec3(-1,-1,1), c=vec3(-1,1,-1), d=vec3(1,1,1);
        // Tetrahedral differences have first-order bias; use a smaller step
        // than the second-order central stencil, retaining a precision floor.
        float normal_step=max(0.00001,epsilon*0.25);
        normal=a*distance_at(point+a*normal_step)+b*distance_at(point+b*normal_step)
              +c*distance_at(point+c*normal_step)+d*distance_at(point+d*normal_step);
        normal_evaluations+=4;
    } else if(!analytic_normal) {
        normal=vec3(distance_at(point+e.xyy)-distance_at(point-e.xyy),
                    distance_at(point+e.yxy)-distance_at(point-e.yxy),
                    distance_at(point+e.yyx)-distance_at(point-e.yyx));
        normal_evaluations+=6;
    }
    float n2=dot(normal,normal);
    if(n2<1e-16||isnan(n2)||isinf(n2)) discard;
    normal*=inversesqrt(n2);
    vec3 world_normal=normalize(v_inverse0.xyz*normal.x+v_inverse1.xyz*normal.y+v_inverse2.xyz*normal.z);
    vec3 side=abs(normal);
    vec3 uv_dimensions=u_dimensions.xyz;
#ifdef SG_MODEL_ISOLATE
    uv_dimensions=u_preview_uv_bounds[1].xyz;
    local=(point-u_preview_uv_bounds[0].xyz)/uv_dimensions;
#endif
    vec2 uv=side.y>=side.x&&side.y>=side.z ? local.xz+0.5
        : side.x>=side.z ? vec2(local.z+0.5,0.5-local.y)
        : vec2(local.x+0.5,0.5-local.y);
    vec4 geometry_parameters[32], resolved[SG_OUTPUTS];
    for(int i=0;i<32;i++) geometry_parameters[i]=SG_SHAPE_PARAMETER(i);
    SGSample hit_context=shape_context(point);
#ifdef SG_MODEL_ISOLATE
    sg_preview_hit(hit_context,geometry_parameters,resolved);
#else
    sg_model_hit_dispatch(int(u_shape.x),hit_context,geometry_parameters,resolved);
#endif
    int binding_index=sg_model_material_slot(int(u_shape.x),int(resolved[0].x))+1;
    if(binding_index<0||binding_index>32) discard;
    vec4 binding=u_model_materials[binding_index];
    int offset=int(binding.y), count=int(binding.z);
    vec4 material_parameters[32];
    for(int i=0;i<32;i++) material_parameters[i]=i<count ? SG_MATERIAL_PARAMETER(offset+i) : vec4(0);
    vec3 object_rows[3], part_object_rows[3], part_rows[3], uv_rows[2];
    surface_object_frame(object_rows);
    sg_model_frame_dispatch(int(u_shape.x),int(resolved[0].x),hit_context,geometry_parameters,part_object_rows);
    for(int i=0;i<3;i++) part_rows[i]=part_object_rows[i].x*object_rows[0]+part_object_rows[i].y*object_rows[1]+part_object_rows[i].z*object_rows[2];
    uv_rows[0]=(side.y>=side.x&&side.y>=side.z || side.x<side.z) ? object_rows[0]/uv_dimensions.x : object_rows[2]/uv_dimensions.z;
    uv_rows[1]=side.y>=side.x&&side.y>=side.z ? object_rows[2]/uv_dimensions.z : -object_rows[1]/uv_dimensions.y;
    shade_surface_points(world,world_normal,point,resolved[1].xyz,uv,object_rows,part_rows,uv_rows,int(binding.x),material_parameters);
    if(u_sdf_quality.z>0.5) {
        // Optional capture: red is trace evaluations / 128, green marks a hit,
        // blue is normal graph calls / 8 (including a differentiated graph).
        // Misses retain the clear target; no atomics or readback affect normal rendering.
        result_scene=vec4(float(evaluations)/128.0,1,float(normal_evaluations)/8.0,1);
    }
}
