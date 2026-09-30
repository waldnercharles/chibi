#include "editor/collab/collab.h"
#include "editor/collab/protocol.h"
#include "support/xalloc.h"
#include "test/support/report.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int applies, restores, closes, sends, value;
    bool fail_apply, fail_restore;
    char *last;
} fixture;

static void send(void *context, const char *json)
{
    fixture *f = context;
    free(f->last);
    f->last = xstrdup(json);
    f->sends++;
}

static bool apply(void *context, const char *, char *, size_t)
{
    fixture *f = context;
    assert(collab_replaying());
    assert(collab_record_history() == collab_is_host());
    assert(*collab_apply_client());
    int before = f->sends;
    assert(collab_publish("{\"family\":\"test\"}"));
    assert(f->sends == before); /* replay never recursively publishes */
    if (f->fail_apply) {
        return false;
    }
    f->applies++;
    f->value++;
    return true;
}

static char *checkpoint(void *)
{
    return xstrdup("{\"version\":1}");
}

static bool restore(void *context, const char *, char *, size_t)
{
    fixture *f = context;
    assert(collab_replaying());
    if (f->fail_restore) {
        return false;
    }
    f->restores++;
    f->value = 0;
    return true;
}

static void closed(void *context)
{
    ((fixture *)context)->closes++;
}

static void receive(const char *kind, const char *client, uint64_t seq, uint64_t revision, const char *payload)
{
    char *json = collab_message_write("room", client, seq, revision, kind, payload);
    assert(json);
    collab_receive(json);
    free(json);
    collab_tick();
}

static void begin(fixture *f, const char *role)
{
    collab_shutdown();
    free(f->last);
    *f = (fixture){ 0 };
    collab_init((collab_config){ .context = f,
                                 .send = send,
                                 .apply = apply,
                                 .checkpoint = checkpoint,
                                 .restore = restore,
                                 .closed = closed });
    collab_start("room", "self", role);
}

static void viewer_ready(fixture *f, const char *role)
{
    begin(f, role);
    char body[128];
    snprintf(body, sizeof body, "{\"role\":\"%s\",\"next_seq\":1,\"snapshot_revision\":0}", role);
    receive("joined", "self", 0, 0, body);
    assert(collab_state_current() == COLLAB_LOADING);
    receive("snapshot", "host", 0, 0, "{\"checkpoint_version\":1,\"workspace\":\"{}\",\"sha256\":\"hash\"}");
    assert(collab_state_current() == COLLAB_READY && f->restores == 1);
}

void test_collab_replay()
{
    fixture f = { 0 };
    viewer_ready(&f, "viewer");
    assert(!collab_can_edit() && !collab_propose("{}"));
    receive("accepted", "host", 1, 1, "{\"family\":\"test\"}");
    assert(f.applies == 1 && collab_revision() == 1 && !collab_replaying());
    receive("accepted", "host", 1, 1, "{\"family\":\"test\"}");
    assert(f.applies == 1); /* duplicate delivery is idempotent */
    receive("accepted", "host", 3, 3, "{\"family\":\"test\"}");
    assert(f.applies == 1 && collab_state_current() == COLLAB_RESYNCING);
    receive("accepted", "host", 2, 2, "{\"family\":\"test\"}");
    assert(f.applies == 1); /* delayed action cannot unfreeze a gap */
    receive("snapshot", "host", 0, 3, "{\"checkpoint_version\":1,\"workspace\":\"{}\"}");
    assert(f.restores == 2 && collab_revision() == 3);
    receive("accepted", "host", 4, 4, "{\"family\":\"test\"}");
    assert(f.applies == 2 && collab_revision() == 4);
    f.fail_apply = true;
    receive("accepted", "host", 5, 5, "{}");
    assert(collab_revision() == 4 && collab_state_current() == COLLAB_RESYNCING);
    f.fail_restore = true;
    receive("snapshot", "host", 0, 5, "{\"checkpoint_version\":1,\"workspace\":\"{}\"}");
    assert(collab_revision() == 4 && f.restores == 2);
    receive("room_closed", "server", 0, 5, "{}");
    assert(!collab_active() && f.closes == 1 && collab_can_edit());

    viewer_ready(&f, "editor");
    assert(collab_propose("{\"family\":\"test\"}") && !collab_can_edit());
    assert(f.applies == 0);
    receive("accepted", "self", 1, 1, "{\"family\":\"test\"}");
    assert(f.applies == 1 && collab_can_edit());
    assert(collab_propose("{}"));
    receive("rejected", "self", 2, 1, "{\"reason\":\"Stale base\"}");
    assert(f.applies == 1 && collab_can_edit() && !strcmp(collab_status(), "Stale base"));
    collab_disconnect("Offline");
    assert(!collab_can_edit());
    collab_leave();
    assert(f.closes == 0); /* shutdown restores only on the frame thread */
    collab_tick();
    assert(f.closes == 1);

    begin(&f, "host");
    collab_seed("{\"checkpoint_version\":1,\"workspace\":\"{}\",\"project_root\":\"/collab/project\"}");
    assert(f.restores == 0);
    collab_tick();
    assert(f.restores == 1 && f.sends == 0);
    assert(collab_state_current() == COLLAB_CONNECTING);
    receive("joined", "self", 0, 0, "{\"role\":\"host\",\"next_seq\":1,\"snapshot_revision\":null}");
    assert(!collab_can_edit());
    receive("snapshot_ack", "server", 0, 0, "{}");
    assert(collab_can_edit());
    f.value++;
    assert(collab_publish("{\"family\":\"test\"}"));
    assert(!collab_can_edit());
    receive("accepted", "self", 1, 1, "{\"family\":\"test\"}");
    assert(f.applies == 0 && f.value == 1); /* already applied host echo */
    receive("proposal", "other", 1, 1, "{\"family\":\"test\"}");
    assert(f.applies == 1 && !collab_can_edit());
    receive("accepted", "other", 1, 2, "{\"family\":\"test\"}");
    assert(f.applies == 1 && collab_revision() == 2 && collab_can_edit());
    receive("proposal", "other", 2, 1, "{}");
    assert(f.applies == 1 && strstr(f.last, "rejected"));
    f.fail_apply = true;
    receive("proposal", "other", 3, 2, "{}");
    assert(f.applies == 1 && strstr(f.last, "rejected"));
    collab_begin_replay();
    collab_begin_replay();
    collab_end_replay();
    assert(collab_replaying());
    collab_end_replay();
    assert(!collab_replaying());
    collab_shutdown();
    free(f.last);
    test_note("collaboration replay PASS: order, duplicates, rejection, gaps, restore, non-recursion, departure\n");
}
