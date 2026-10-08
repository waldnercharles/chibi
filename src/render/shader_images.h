#ifndef CHIBI_SHADER_IMAGES_H
#define CHIBI_SHADER_IMAGES_H
#include "platform/cute.h"
#include "runtime/images/catalog.h"
CF_Texture render_image_texture(int variant, shader_error *error);
void render_images_collect();
void render_images_clear();
#endif
