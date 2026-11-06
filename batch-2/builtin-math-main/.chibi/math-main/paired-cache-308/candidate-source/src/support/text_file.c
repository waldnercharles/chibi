#include "support/text_file.h"

#include "support/file_io.h"
#include "support/text_overlay.h"
#include "support/xalloc.h"

char *text_file_read(const char *path)
{
    const char *overlay = text_overlay_text(path);
    return overlay ? xstrdup(overlay) : file_read_text(path);
}
