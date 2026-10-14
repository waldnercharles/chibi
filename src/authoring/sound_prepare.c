#include "authoring/sound_prepare.h"

#include "authoring/visual_prepare.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "vendor/sound_vorbis.h"

static bool fail(char *error, size_t size, const char *text)
{
    if (size) {
        snprintf(error, size, "%s", text);
    }
    return false;
}
static int compare(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}
static bool extension(const char *name, const char *suffix)
{
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, suffix);
}
static sound_format format(const char *name)
{
    return extension(name, ".ogg") ? SOUND_OGG : SOUND_WAV;
}
static unsigned char *read_file(const char *path, size_t limit, size_t *length)
{
    struct stat info;
    if (stat(path, &info) || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
        (size_t)info.st_size > limit) {
        return NULL;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    *length = (size_t)info.st_size;
    unsigned char *bytes = xmalloc(*length);
    bool read = fread(bytes, 1, *length, file) == *length;
    if (fclose(file) || !read) {
        free(bytes);
        return NULL;
    }
    return bytes;
}
/* Decodes the whole stream as players do: one their decoder cannot open, or
 * whose audio packets all fail, plays nothing. */
static bool vorbis_plays(const unsigned char *bytes, size_t length)
{
    stb_vorbis *vorbis = chibi_sound_vorbis_open_memory(bytes, (int)length, NULL, NULL);
    if (!vorbis) {
        return false;
    }
    bool sound = false;
    float **samples;
    while (chibi_sound_vorbis_get_frame_float(vorbis, NULL, &samples) > 0) {
        sound = true;
    }
    chibi_sound_vorbis_close(vorbis);
    return sound;
}
static bool named(const player_asset *assets, int from, const char *name)
{
    for (int i = from; i < acount(assets); i++) {
        if (!strcmp(assets[i].name, name)) {
            return true;
        }
    }
    return false;
}
static bool add_sound(
    const char *directory,
    const char *file,
    player_asset **assets,
    int before,
    size_t *total,
    char *error,
    size_t size
)
{
    char path[4096];
    char *name = xstrdup(file);
    name[strlen(name) - 4] = 0;
    size_t length = 0;
    unsigned char *bytes = NULL;
    const char *reason = NULL;
    if (named(*assets, before, name)) {
        reason = "another sound has this name";
    } else if (
        acount(*assets) == PLAYER_MAX_ASSETS ||
        snprintf(path, sizeof path, "%s/%s", directory, file) >= (int)sizeof path ||
        !(bytes = read_file(path, PLAYER_MAX_ASSET_BYTES - *total, &length))
    ) {
        reason = "cannot be read within the resource limit";
    } else if (sound_format_of(bytes, length) != format(file)) {
        /* A file must hold what its extension says, in a form players decode. */
        reason = format(file) == SOUND_OGG
                   ? "is not a complete mono or stereo 44.1 kHz Ogg Vorbis file"
                   : "is not a plain mono or stereo 44.1 kHz PCM or float WAV "
                     "file";
    } else if (format(file) == SOUND_OGG && !vorbis_plays(bytes, length)) {
        reason = "holds no Vorbis audio that players can decode";
    }
    if (reason) {
        snprintf(error, size, "sounds/%s: %s", file, reason);
        free(name);
        free(bytes);
        return false;
    }
    apush(
        *assets,
        ((player_asset){ .name = name,
                         .kind = PLAYER_ASSET_SOUND,
                         .data = bytes,
                         .length = length,
                         .content_hash = player_asset_fingerprint(bytes, length) })
    );
    *total += length;
    return true;
}
bool authoring_sounds_prepare(const char *content, player_asset **assets, char *error, size_t size)
{
    char directory[4096];
    if (snprintf(directory, sizeof directory, "%s/sounds", content) >=
        (int)sizeof directory) {
        return fail(error, size, "Sound directory path exceeds limit");
    }
    DIR *dir = opendir(directory);
    if (!dir) {
        return errno == ENOENT ||
               fail(error, size, "Cannot open the sounds directory");
    }
    char **files = NULL;
    for (struct dirent *entry; (entry = readdir(dir));) {
        const char *name = entry->d_name;
        if (name[0] != '.' && (extension(name, ".wav") || extension(name, ".ogg"))) {
            apush(files, xstrdup(name));
        }
    }
    closedir(dir);
    if (acount(files) > 1) {
        qsort(files, (size_t)acount(files), sizeof *files, compare);
    }
    size_t total = 0;
    for (int i = 0; i < acount(*assets); i++) {
        total += (*assets)[i].length;
    }
    int before = acount(*assets);
    bool valid = true;
    for (int i = 0; valid && i < acount(files); i++) {
        valid = add_sound(directory, files[i], assets, before, &total, error, size);
    }
    for (int i = 0; i < acount(files); i++) {
        free(files[i]);
    }
    afree(files);
    return valid;
}
bool authoring_sounds_load(const char *content, audio *audio, char *error, size_t size)
{
    player_asset *sounds = NULL;
    bool valid = authoring_sounds_prepare(content, &sounds, error, size);
    for (int i = 0; valid && i < acount(sounds); i++) {
        audio_add_sound(audio, sounds[i].name, sounds[i].data, sounds[i].length);
    }
    authoring_visual_prepared_free(sounds);
    return valid;
}
