#ifndef CHIBI_RENDER_VIEW_2D_H
#define CHIBI_RENDER_VIEW_2D_H

/* A padded Y-up world rectangle shared by every 2D render pass. The crop is
 * the camera rectangle in normalized texture coordinates. */
typedef struct
{
    float min_x;
    float max_y;
    float width;
    float height;
    float crop_min_u;
    float crop_min_v;
    float crop_max_u;
    float crop_max_v;
} render_view_2d;

#endif
