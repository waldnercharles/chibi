#include "render/sdf_shapes.h"

#include "platform/cute.h"
#include "render/appearance_geometry.h"
#include "render/sdf_motion.h"

#include <assert.h>
#include <math.h>

static const char source[] =
    "float sdf(vec2 p, ShapeParams s) {\n"
    "  p-=s.a; float c=cos(s.c.x), sn=sin(s.c.x);\n"
    "  p=vec2(c*p.x+sn*p.y,-sn*p.x+c*p.y);\n"
    "  float angle=atan(p.y,p.x), frequency=round(s.e.x), deform=0.0;\n"
    "  if(s.d.y>0.0) deform=sin(angle*frequency-s.f.x*s.e.y)*s.d.y;\n"
    "  if(s.f.y>0.01) {\n"
    "    float dist=angle-s.g.x;\n"
    "    dist-=6.2831853*floor((dist+3.1415926)/6.2831853);\n"
    "    float front=s.g.y*s.h.x, p1=dist-front, p2=dist+front;\n"
    "    float env=0.8+frequency*0.1;\n"
    "    deform+=(sin(dist*frequency-front)*exp(-p1*p1*env)\n"
    "            +sin(dist*frequency+front)*exp(-p2*p2*env))*s.f.y;\n"
    "  }\n"
    "  vec2 radial=p+vec2(0.0001); float radius=length(radial);\n"
    "  if(radius>0.0) p+=radial/radius*deform;\n"
    "  vec2 half_size=s.b*0.5; int shape=int(s.c.y);\n"
    "  if(shape==1) return length(p)-min(half_size.x,half_size.y);\n"
    "  if(shape==5) {\n"
    "    float r=min(half_size.x,half_size.y);\n"
    "    vec2 axis=half_size-vec2(r);\n"
    "    vec2 e=axis*2.0; float q=dot(e,e);\n"
    "    return length(p+axis-e*(q>0.0 ? clamp(dot(p+axis,e)/q,0.0,1.0) : "
    "0.0))-r;\n"
    "  }\n"
    "  vec2 v[4];\n"
    "  if(shape==2) {\n"
    "    v[0]=vec2(half_size.x,0); v[1]=vec2(0,half_size.y);\n"
    "    v[2]=-v[0]; v[3]=-v[1];\n"
    "  }\n"
    "  if(shape==3 || shape==4) {\n"
    "    if(shape==4) {\n"
    "      v[0]=vec2(s.b.x*0.8,0); v[1]=vec2(-s.b.x*0.2,-half_size.y);\n"
    "      v[2]=vec2(0); v[3]=vec2(-s.b.x*0.2,half_size.y);\n"
    "      float d=1e30;\n"
    "      for(int i=0;i<4;i++) {\n"
    "        vec2 e=v[(i+1)%4]-v[i], w=p-v[i];\n"
    "        d=min(d,length(w-e*clamp(dot(w,e)/dot(e,e),0.0,1.0)));\n"
    "      }\n"
    "      return d-s.d.x*0.5;\n"
    "    }\n"
    "    v[0]=vec2(half_size.x,0); v[1]=vec2(-half_size.x,half_size.y);\n"
    "    v[2]=vec2(-half_size.x*0.6,0); v[3]=vec2(-half_size.x,-half_size.y);\n"
    "  }\n"
    "  if(shape==2 || shape==3) {\n"
    "    float d=1e30; bool inside=false;\n"
    "    for(int i=0,j=3;i<4;j=i,i++) {\n"
    "      vec2 e=v[i]-v[j], w=p-v[j];\n"
    "      d=min(d,length(w-e*clamp(dot(w,e)/dot(e,e),0.0,1.0)));\n"
    "      if((v[i].y>p.y && v[j].y<=p.y) || (v[j].y>p.y && v[i].y<=p.y)) {\n"
    "        float x=(v[j].x-v[i].x)*(p.y-v[i].y)/(v[j].y-v[i].y)+v[i].x;\n"
    "        if(p.x<x) inside=!inside;\n"
    "      }\n"
    "    }\n"
    "    return inside ? -d : d;\n"
    "  }\n"
    "  vec2 q=abs(p)-half_size;\n"
    "  return length(max(q,vec2(0)))+min(max(q.x,q.y),0.0);\n"
    "}\n";

app_sdf_shapes app_sdf_shapes_init()
{
    return cf_make_custom_shape(source);
}

bool app_sdf_shapes_draw(
    app_sdf_shapes shapes,
    const GameAppearance *appearance,
    const GameTransform *transform,
    float radians,
    const GameSdfMotion *motion
)
{
    assert(appearance && transform && motion);
    CF_V2 min, max;
    float padding;
    if (!shapes.id || !isfinite(transform->x) || !isfinite(transform->y) ||
        !game_appearance_rotated_bounds(appearance, radians, &min, &max) ||
        !game_sdf_motion_padding(motion, &padding) ||
        !isfinite(motion->time * motion->temporal_frequency) ||
        !isfinite(motion->pluck_age * motion->pluck_speed)) {
        return false;
    }
    CF_Aabb bounds = {
        { min.x + transform->x - padding, min.y - transform->y - padding },
        { max.x + transform->x + padding, max.y - transform->y + padding },
    };
    if (!isfinite(bounds.min.x) || !isfinite(bounds.min.y) ||
        !isfinite(bounds.max.x) || !isfinite(bounds.max.y)) {
        return false;
    }
    /* a=center, b=size, c=rotation/shape, d=stroke/amplitude, e=frequency/time rate,
     * f=time/pluck amplitude, g=pluck origin/age, h=pluck speed/reserved. */
    float params[16] = {
        transform->x,
        -transform->y,
        appearance->width,
        appearance->height,
        radians,
        (float)appearance->shape,
        appearance->shape == GAME_SHAPE_STROKED_CHEVRON ? appearance->stroke_width : 0,
        motion->amplitude,
        motion->angular_frequency,
        motion->temporal_frequency,
        motion->time,
        motion->pluck_amplitude,
        motion->pluck_origin,
        motion->pluck_age,
        motion->pluck_speed,
        0,
    };
    cf_draw_custom_shape_fill(shapes, bounds, params, 16);
    return true;
}
