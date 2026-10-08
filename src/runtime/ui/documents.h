#ifndef CHIBI_PREPARED_UI_DOCUMENTS_H
#define CHIBI_PREPARED_UI_DOCUMENTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define GAME_UI_DOCUMENT_VERSION UINT64_C(1)
typedef enum
{
    GAME_UI_RESOURCE_IMAGE = 1,
    GAME_UI_RESOURCE_FONT
} game_ui_resource_kind;
typedef struct
{
    game_ui_resource_kind kind;
    char *key;
} game_ui_resource;
/* Owning pre-expanded document. Widgets and RCSS imports have been expanded by
 * the authoring toolchain; body attributes are present only for page documents.
 * RmlUi interprets these prepared texts into its DOM and styles. Dependencies
 * resolve to resident bytes before UI publication. Fonts must remain alive until
 * RmlUi retires their faces; images until all corresponding documents retire. */
typedef struct
{
    char *name, *markup, *style, *body;
    game_ui_resource *resources;
} game_ui_document;
void game_ui_document_free(game_ui_document *);
bool game_ui_document_prepared_write(
    const game_ui_document *,
    unsigned char **,
    size_t *,
    char *error,
    size_t
);
bool game_ui_document_prepared_read_budget(
    const void *,
    size_t,
    size_t *remaining,
    game_ui_document *,
    char *error,
    size_t
);
/* Detached catalog. Successful insertion consumes a validated document;
 * failure keeps caller ownership. Exchange cannot fail; the exchanged-out
 * catalog retains old document addresses until the host retires that catalog. */
typedef struct game_ui_documents game_ui_documents;
game_ui_documents *game_ui_documents_new();
/* active=false creates an empty retirement shell; active=true is authoritative
 * even when empty. Budget failures leave caller ownership/remainder unchanged. */
game_ui_documents *game_ui_documents_new_budget(size_t *remaining, bool active);
bool game_ui_documents_add_owned_budget(game_ui_documents *, game_ui_document *, size_t *remaining);
bool game_ui_documents_add_owned(game_ui_documents *, game_ui_document *);
void game_ui_documents_exchange(game_ui_documents *);
void game_ui_documents_free(game_ui_documents *);
const game_ui_documents *game_ui_documents_current();
const game_ui_document *game_ui_documents_find(const game_ui_documents *, const char *);
int game_ui_documents_count(const game_ui_documents *);
const game_ui_document *game_ui_documents_at(const game_ui_documents *, int);
/* A prepared catalog is authoritative, including an intentionally empty one.
 * Missing documents must fail instead of loading authored project files. */
bool game_ui_documents_active();
uint64_t game_ui_documents_revision();
#endif
