#include "render/shader_fragment.h"

#include "render/shader_catalog.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *shader_source(const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", CBLUEPRINT_RENDER_SHADER_DIR, name);
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Cannot read shader source: %s\n", path);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    char *source = xmalloc((size_t)size + 1);
    size_t read = fread(source, 1, (size_t)size, file);
    fclose(file);
    source[read] = 0;
    return source;
}

CF_Shader render_shader_fragment_new()
{
    char *raster = shader_source("hrc/entity_raster.shd");
    if (!raster) {
        return (CF_Shader){ 0 };
    }
    char *start = strstr(raster, "float sdf_polyline"),
         *end = strstr(raster, "#include \"color.shd\"");
    if (!start || !end) {
        free(raster);
        return (CF_Shader){ 0 };
    }
    *end = 0;
    char *graphs = render_shader_glsl();
    static const char body[] =
        "layout(set=3,binding=1) uniform shd_uniforms { vec4 sg_entity[84]; "
        "};\n"
        "vec4 shader(vec4 color,ShaderParams params) {\n"
        " Entity e; e.inv_rot=sg_entity[0]; e.pos=sg_entity[1].xy; "
        "e.extent=sg_entity[1].zw;\n"
        " e.shape_type=uint(sg_entity[2].x); e.radius=sg_entity[2].y; "
        "e.thickness=sg_entity[2].z; e.chubbiness=sg_entity[2].w;\n"
        " e.aa=sg_entity[3].x; e.point_count=uint(sg_entity[3].y); "
        "e.border_thickness=sg_entity[4].x;\n"
        " e.points_01=sg_entity[5]; e.points_23=sg_entity[6]; "
        "e.points_45=sg_entity[7]; e.points_67=sg_entity[8];\n"
        " vec2 wp=params.view_pos, p=wp-e.pos; "
        "p=vec2(dot(e.inv_rot.xy,p),dot(e.inv_rot.zw,p));\n"
        " vec4 wobble=sg_entity[17],pluck=sg_entity[18]; float "
        "clock=sg_entity[19].x;\n"
        " float angle=atan(p.y,p.x),freq=round(wobble.y),deform=0.0;\n"
        " if(wobble.x>0.0) deform=sin(angle*freq-clock*wobble.z)*wobble.x;\n"
        " if(pluck.x>0.01) {float "
        "dist=angle-pluck.y;dist-=6.2831853*floor((dist+3.1415926)/"
        "6.2831853);\n"
        " float "
        "front=pluck.z*pluck.w,env=0.8+freq*0.1,p1=dist-front,p2=dist+front;\n"
        " deform+=(sin(dist*freq-front)*exp(-p1*p1*env)+sin(dist*freq+front)*"
        "exp(-p2*p2*env))*pluck.x;}\n"
        " vec2 "
        "radial=p+vec2(0.0001);if(length(radial)>0.0)p+=radial/"
        "length(radial)*deform;\n"
        " float d=evaluate_sdf(p,e); vec4 values[32], result[8];\n"
        " int shape=int(sg_entity[4].z), material=int(sg_entity[4].y); float "
        "time=sg_entity[19].x;\n"
        " if(shape>0) { for(int i=0;i<32;i++) values[i]=sg_entity[52+i]; "
        "sg_dispatch(shape,p,wp,time,values,result); d=result[0].x; }\n"
        " vec4 "
        "sc=sg_entity[9],em=sg_entity[10],bs=sg_entity[13],be=sg_entity[14];\n"
        " if(material>0) { for(int i=0;i<32;i++) values[i]=sg_entity[20+i]; "
        "sg_dispatch(material,p,wp,time,values,result); sc=result[0]; "
        "em=result[1]; bs=result[4]; be=result[5]; }\n"
        " float aa=max(e.aa,0.001), coverage=1.0-smoothstep(0.0,aa,d);\n"
        " float "
        "fill=e.border_thickness>0.0?1.0-smoothstep(-e.border_thickness,-e."
        "border_thickness+aa,d):coverage;\n"
        " float border=coverage-fill;\n"
        " vec4 a=vec4(sc.rgb+em.rgb*em.a,sc.a)*fill; vec4 "
        "b=vec4(bs.rgb+be.rgb*be.a,bs.a)*border;\n"
        " return vec4(a.rgb+b.rgb,a.a+b.a);\n}\n";
    char *entity = shader_source("entity.shd"), *sdf = shader_source("sdf.shd");
    if (!entity || !sdf) {
        free(entity);
        free(sdf);
        free(graphs);
        free(raster);
        return (CF_Shader){ 0 };
    }
    size_t bytes = strlen(entity) + strlen(sdf) + strlen(start) +
                   strlen(graphs) + sizeof body + 8;
    char *source = xmalloc(bytes);
    snprintf(source, bytes, "%s\n%s\n%s\n%s\n%s", entity, sdf, start, graphs, body);
    free(entity);
    free(sdf);
    CF_Shader shader = cf_make_draw_shader_from_source(source);
    free(source);
    free(graphs);
    free(raster);
    return shader;
}
void render_shader_fragment_draw(CF_Shader shader, const render_material_entity *entity)
{
    float data[336];
    memcpy(data, entity, sizeof data);
    data[8] = (float)entity->shape_type;
    data[13] = (float)entity->point_count;
    cf_draw_push_shader(shader);
    cf_draw_set_uniform("sg_entity", data, CF_UNIFORM_TYPE_FLOAT4, 84);
    CF_Aabb bounds = {
        { entity->pos[0] - entity->extent[0], entity->pos[1] - entity->extent[1] },
        { entity->pos[0] + entity->extent[0], entity->pos[1] + entity->extent[1] }
    };
    cf_draw_box_fill(bounds, 0);
    cf_draw_pop_shader();
}
