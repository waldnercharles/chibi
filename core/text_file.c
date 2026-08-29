#include "text_file.h"

#include "xalloc.h"

#include <stdio.h>
#include <stdlib.h>

char *text_file_read(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) {
        if (file) {
            fclose(file);
        }
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *text = (char *)xmalloc((size_t)length + 1);
    size_t read = fread(text, 1, (size_t)length, file);
    int closed = fclose(file);
    if (read != (size_t)length || closed != 0) {
        free(text);
        return NULL;
    }
    text[length] = '\0';
    return text;
}
