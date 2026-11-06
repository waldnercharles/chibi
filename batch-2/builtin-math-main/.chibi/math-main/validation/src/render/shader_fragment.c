#include "render/shader_fragment.h"

#include "render/shader_catalog.h"
#include "support/xalloc.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *render_shader_fragment_source()
{
    char *raster = render_shader_source("hrc/entity_raster.frag");
    if (!raster) {
        return NULL;
    }
    char *start = strstr(raster, "float sdf_polyline"),
         *end = strstr(raster, "#include \"color.shd\"");
    if (!start || !end) {
        free(raster);
        return NULL;
    }
    *end = 0;
    static const char body[] =
        "vec4 shader(vec4 color,ShaderParams params) {\n"
        " Entity e; e.inv_rot=sg_entity[0]; e.pos=sg_entity[1].xy; "
        "e.extent=sg_entity[1].zw;\n"
        " e.shape_type=uint(sg_entity[2].x); e.radius=sg_entity[2].y; "
        "e.thickness=sg_entity[2].z; e.relief=sg_entity[2].w;\n"
        " e.aa=sg_entity[3].x; e.point_count=uint(sg_entity[3].y); "
        "e.border_thickness=sg_entity[4].x;\n"
        " e.points_01=sg_entity[5]; e.points_23=sg_entity[6]; "
        "e.points_45=sg_entity[7]; e.points_67=sg_entity[8];\n"
        " vec2 wp=params.view_pos, p=wp-e.pos; "
        "p=vec2(dot(e.inv_rot.xy,p),dot(e.inv_rot.zw,p));\n"
        " float d=evaluate_sdf(p,e); vec2 half_size=shader_half_size(e); vec4 "
        "values[32], result[SG_OUTPUTS];\n"
        " int shape=int(sg_entity[4].z), material=int(sg_entity[4].y); float "
        "time=sg_entity[17].x;\n"
        " e.material_frame=sg_entity[18]; vec2 "
        "mp=material_position(e,p,wp);\n"
        " if(shape>0) { for(int i=0;i<32;i++) values[i]=sg_entity[51+i]; "
        "sg_dispatch_2d(shape,p,mp,half_size,e.inv_rot,time,values,result); "
        "d=result[0].x; }\n"
        " vec4 "
        "sc=sg_entity[9],em=sg_entity[10],bs=sg_entity[13],be=sg_entity[14];\n"
        " if(material>0) { for(int i=0;i<32;i++) values[i]=sg_entity[19+i]; "
        "sg_dispatch_2d(material,p,mp,half_size,e.inv_rot,time,values,"
        "result); "
        "sc=result[0]; "
        "em=result[1]; bs=result[0]; be=result[1]; }\n"
        " if(e.shape_type==SHAPE_SPRITE) { vec4 "
        "t=d>0.0?vec4(0):sprite_texel(p,e); "
        "sc*=t; em.a*=t.a; d=-1.0; }\n"
        " float aa=max(e.aa,0.001), coverage=1.0-smoothstep(0.0,aa,d);\n"
        " float "
        "fill=e.border_thickness>0.0?1.0-smoothstep(-e.border_thickness,-e."
        "border_thickness+aa,d):coverage;\n"
        " float border=coverage-fill;\n"
        " vec4 "
        "a=vec4(sc.rgb*sc.a*sg_lighting.x+em.rgb*em.a*sg_lighting.y,sc.a)*fill;"
        " vec4 "
        "b=vec4(bs.rgb*bs.a*sg_lighting.x+be.rgb*be.a*sg_lighting.y,bs.a)*"
        "border;\n"
        " return vec4(a.rgb+b.rgb,a.a+b.a);\n}\n";
    char *entity = render_shader_source("entity.shd"),
         *sdf = render_shader_source("sdf.shd");
    if (!entity || !sdf) {
        free(entity);
        free(sdf);
        free(raster);
        return NULL;
    }
    size_t bytes = strlen(entity) + strlen(sdf) + strlen(start) + sizeof body + 8;
    char *source = xmalloc(bytes);
    snprintf(source, bytes, "%s\n%s\n%s\n%s", entity, sdf, start, body);
    free(entity);
    free(sdf);
    free(raster);
    return source;
}
int render_shader_fragment_prepare(
    render_programs *programs,
    const render_material_entity *entity,
    shader_error *error
)
{
    render_shader_set set = { 0 };
    if (!render_shader_set_add(&set, (int)entity->reserved[0], error) ||
        !render_shader_set_add(&set, (int)entity->reserved[1], error) ||
        (entity->shape_type == RENDER_SHAPE_SPRITE &&
         !render_shader_set_add_image(&set, (int)entity->points[12], error))) {
        return 0;
    }
    int program = render_programs_prepare(programs, &set);
    if (error && render_programs_error(programs)[0]) {
        snprintf(error->message, sizeof error->message, "%s", render_programs_error(programs));
    }
    return program;
}

void render_shader_fragment_draw(
    render_programs *programs,
    int program,
    const render_material_entity *entity,
    CF_V2 lighting
)
{
    /* Everything but the layer, which only orders draws. */
    float data[offsetof(render_material_entity, layer) / sizeof(float)];
    memcpy(data, entity, sizeof data);
    data[8] = (float)entity->shape_type;
    data[13] = (float)entity->point_count;
    render_programs_bind2d(programs, program);
    cf_draw_set_uniform("sg_entity", data, CF_UNIFORM_TYPE_FLOAT4, (int)(sizeof data / 16));
    cf_draw_set_uniform("sg_lighting", &lighting, CF_UNIFORM_TYPE_FLOAT2, 1);
    CF_Aabb bounds = {
        { entity->pos[0] - entity->extent[0], entity->pos[1] - entity->extent[1] },
        { entity->pos[0] + entity->extent[0], entity->pos[1] + entity->extent[1] }
    };
    cf_draw_box_fill(bounds, 0);
    render_programs_unbind2d(programs, program);
}
