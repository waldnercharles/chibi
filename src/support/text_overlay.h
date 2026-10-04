#ifndef CHIBI_TEXT_OVERLAY_H
#define CHIBI_TEXT_OVERLAY_H

/* Unsaved text read in place of files, as an editor's open buffers feed a
 * language server: text_file_read returns a path's overlay, and
 * file_watch_changed sees each change to it at once. A path matches however
 * it is spelled: relative to the working directory or absolute, with or
 * without "." and ".." parts. Nothing is copied or compared while no overlay
 * is set. */

/* Sets `path`'s text to a copy of `text`; NULL removes the overlay, so the
 * file on disk reads again. */
void text_overlay_set(const char *path, const char *text);

/* The overlay of `path`, or NULL. */
const char *text_overlay_text(const char *path);

/* A number that changes with each set or removal of `path`'s overlay, and 0
 * while it has none. */
unsigned long long text_overlay_revision(const char *path);

/* A number that changes with each set or removal of any overlay. */
unsigned long long text_overlay_generation();

/* Removes every overlay. */
void text_overlay_clear();

#endif
