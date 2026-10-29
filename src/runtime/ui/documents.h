#ifndef CHIBI_PREPARED_UI_DOCUMENTS_H
#define CHIBI_PREPARED_UI_DOCUMENTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UI_DOCUMENT_VERSION UINT64_C(2)
typedef enum
{
    UI_RESOURCE_IMAGE = 1,
    UI_RESOURCE_FONT
} ui_resource_kind;
typedef struct
{
    ui_resource_kind kind;
    char *key;
} ui_resource;
/* Owning pre-expanded document. Widgets and RCSS imports have been expanded by
 * the authoring toolchain; body attributes are present only for page documents.
 * RmlUi interprets these prepared texts into its DOM and styles. Dependencies
 * resolve to resident bytes before UI publication. Fonts must remain alive until
 * RmlUi retires their faces; images until all corresponding documents retire. */
typedef struct
{
    char *name, *markup, *style, *body;
    ui_resource *resources;
} prepared_ui_document;
void prepared_ui_document_free(prepared_ui_document *);
bool ui_document_prepared_write(const prepared_ui_document *, unsigned char **, size_t *, char *error, size_t);
bool ui_document_prepared_read_budget(
    const void *,
    size_t,
    size_t *remaining,
    prepared_ui_document *,
    char *error,
    size_t
);
/* Detached catalog. Successful insertion consumes a validated document;
 * failure keeps caller ownership. Exchange cannot fail; the exchanged-out
 * catalog retains old document addresses until the host retires that catalog. */
typedef struct ui_documents ui_documents;
/* active=false creates an empty retirement shell; active=true is authoritative
 * even when empty. Budget failures leave caller ownership/remainder unchanged. */
ui_documents *ui_documents_new_budget(size_t *remaining, bool active);
bool ui_documents_add_owned_budget(ui_documents *, prepared_ui_document *, size_t *remaining);
void ui_documents_exchange(ui_documents *);
void ui_documents_free(ui_documents *);
const ui_documents *ui_documents_current();
const prepared_ui_document *ui_documents_find(const ui_documents *, const char *);
int ui_documents_count(const ui_documents *);
const prepared_ui_document *ui_documents_at(const ui_documents *, int);
/* A prepared catalog is authoritative, including an intentionally empty one.
 * Missing documents must fail instead of loading authored project files. */
bool ui_documents_active();
uint64_t ui_documents_revision();
#endif
