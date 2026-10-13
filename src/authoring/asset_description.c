#include "authoring/asset_description.h"

#include "types/catalog.h"

av_value *authoring_field_describe(av_doc *doc, const type_field *field)
{
    av_value *value = av_obj(doc);
    av_obj_add_str(doc, value, "name", field->name);
    const char *type = pin_type_name(field->type);
    av_obj_add_str(doc, value, "type", type ? type : "flow");
    av_obj_add_uint(doc, value, "offset", field->offset);
    av_obj_add_uint(doc, value, "bytes", field->size);
    if (field->type_name) {
        av_obj_add_str(doc, value, "type_name", field->type_name);
    }
    if (field->description) {
        av_obj_add_str(doc, value, "description", field->description);
    }
    if (field->unit) {
        av_obj_add_str(doc, value, "unit", field->unit);
    }
    av_obj_add_bool(doc, value, "color", field->color);
    if (field->min != field->max) {
        av_value *range = av_obj_add_obj(doc, value, "editing_range");
        av_obj_add_val(doc, range, "min", av_real(doc, field->min));
        av_obj_add_val(doc, range, "max", av_real(doc, field->max));
        av_obj_add_bool(doc, range, "clamp", false);
    }
    const enum_desc *enumeration = field->type == PIN_ENUM && field->type_name
                                     ? type_catalog_enum_find(field->type_name)
                                     : NULL;
    if (enumeration) {
        av_value *choices = av_obj_add_arr(doc, value, "choices");
        for (int i = 0; i < enumeration->value_count; i++) {
            const enum_value_desc *choice = &enumeration->values[i];
            av_value *item = av_arr_add_obj(doc, choices);
            av_obj_add_str(doc, item, "name", choice->name);
            av_obj_add_int(doc, item, "value", choice->value);
            if (choice->label) {
                av_obj_add_str(doc, item, "label", choice->label);
            }
        }
    }
    return value;
}

av_value *authoring_type_describe(av_doc *doc, const type_desc *type)
{
    av_value *value = av_obj(doc);
    av_obj_add_str(doc, value, "name", type->name);
    av_obj_add_uint(doc, value, "bytes", type->size);
    if (type->alias_of) {
        av_obj_add_str(doc, value, "alias_of", type->alias_of);
    }
    av_value *fields = av_obj_add_arr(doc, value, "fields");
    for (int i = 0; i < type->field_count; i++) {
        av_arr_append(fields, authoring_field_describe(doc, &type->fields[i]));
    }
    return value;
}
