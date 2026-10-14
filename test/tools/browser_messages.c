#include "host/browser_messages.h"

#include "authoring/workspace_documents.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdlib.h>
#include <string.h>

static workspace_documents *documents;
static browser_messages *service;
static void cleanup()
{
    browser_messages_free(service);
    workspace_documents_free(documents);
    service = NULL;
    documents = NULL;
}
static void setup()
{
    workspace_document_status status;
    documents = workspace_documents_new(NULL, &status);
    char error[256];
    service = browser_messages_new("/tmp", documents, error, sizeof error);
}
TEST_CASE(browser_metadata_is_owned_and_channel_order_is_preserved)
{
    REQUIRE(service);
    int instance = browser_messages_instance(service);
    char text[] = "owned";
    REQUIRE(browser_messages_deliver(instance, 1, "first", "result", text, 0));
    memset(text, 'x', sizeof text - 1);
    REQUIRE(browser_messages_deliver(instance, 2, "second", "open", "", 0));
    REQUIRE(browser_messages_deliver(instance, 3, "first", "closed", "reason", 0));
    browser_message message = browser_messages_receive(service, "first");
    REQUIRE(message.available && message.request == 1 && !strcmp(message.text, "owned"));
    message = browser_messages_receive(service, "first");
    REQUIRE(message.available && message.request == 3 && !strcmp(message.kind, "closed"));
    message = browser_messages_receive(service, "second");
    REQUIRE(message.available && message.request == 2 && !strcmp(message.kind, "open"));
    REQUIRE(!browser_messages_receive(service, "first").available);
    return true;
}
TEST_CASE(browser_overflow_is_explicit_and_callbacks_cannot_cross_host_lifetimes)
{
    REQUIRE(service);
    int instance = browser_messages_instance(service);
    for (int i = 0; i < BROWSER_MESSAGES_QUEUE_MAX; i++) {
        REQUIRE(browser_messages_deliver(instance, i + 1, "room", "message", "", 0));
    }
    REQUIRE(!browser_messages_deliver(instance, 99, "room", "message", "", 0));
    browser_message message = browser_messages_receive(service, "room");
    REQUIRE(message.available && !strcmp(message.kind, "overflow"));
    REQUIRE(browser_messages_receive(service, "room").request == 1);
    browser_messages_free(service);
    service = NULL;
    REQUIRE(!browser_messages_deliver(instance, 1, "room", "message", "late", 0));
    char error[256];
    service = browser_messages_new("/tmp", documents, error, sizeof error);
    REQUIRE(service && browser_messages_instance(service) != instance);
    REQUIRE(!browser_messages_deliver(instance, 1, "room", "message", "late", 0));
    REQUIRE(!browser_messages_receive(service, "room").available);
    return true;
}
TEST_CASE(native_browser_operations_refuse_without_fabricated_completion)
{
#ifndef __EMSCRIPTEN__
    REQUIRE(service && !browser_messages_available(service));
    int request = 99;
    char error[256];
    REQUIRE(!browser_messages_request(
        service,
        "room",
        "connect",
        "wss://example.invalid/",
        "",
        "",
        0,
        &request,
        error,
        sizeof error
    ));
    REQUIRE(request == 0 && strstr(error, "unavailable"));
    REQUIRE(!browser_messages_receive(service, "room").available);
    REQUIRE(!browser_messages_release(service, 1));
#endif
    return true;
}
TEST_CASE(browser_metadata_rejects_unbounded_or_invalid_delivery)
{
    REQUIRE(service);
    int instance = browser_messages_instance(service);
    REQUIRE(!browser_messages_deliver(instance, 1, "", "message", "", 0));
    REQUIRE(!browser_messages_deliver(instance, 1, "room", "bad kind", "", 0));
    char *text = malloc(BROWSER_MESSAGES_TEXT_MAX + 2);
    REQUIRE(text);
    memset(text, 'x', BROWSER_MESSAGES_TEXT_MAX + 1);
    text[BROWSER_MESSAGES_TEXT_MAX + 1] = 0;
    bool accepted = browser_messages_deliver(instance, 1, "room", "message", text, 0);
    free(text);
    REQUIRE(!accepted && !browser_messages_receive(service, "room").available);
    return true;
}
TEST_CASE(browser_without_workspace_retains_transport_ownership_but_refuses_capture)
{
    browser_messages_free(service);
    char error[256];
    service = browser_messages_new(NULL, NULL, error, sizeof error);
    REQUIRE(service);
    int request = 0;
    REQUIRE(
        !browser_messages_request(service, "room", "archive-capture", "", "", "", 0, &request, error, sizeof error)
    );
    REQUIRE(!request && strstr(error, "Open a workspace"));
    REQUIRE(
        browser_messages_deliver(browser_messages_instance(service), 1, "room", "open", "", 0)
    );
    browser_message message = browser_messages_receive(service, "room");
    REQUIRE(message.available && message.request == 1 && !strcmp(message.kind, "open"));
    return true;
}
void test_browser_messages()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(browser_without_workspace_retains_transport_ownership_but_refuses_capture);
    RUN_TEST_CASE(browser_metadata_is_owned_and_channel_order_is_preserved);
    RUN_TEST_CASE(browser_overflow_is_explicit_and_callbacks_cannot_cross_host_lifetimes);
    RUN_TEST_CASE(native_browser_operations_refuse_without_fabricated_completion);
    RUN_TEST_CASE(browser_metadata_rejects_unbounded_or_invalid_delivery);
    pu_clear_setup();
}
