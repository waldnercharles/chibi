#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <yyjson.h>

static char directory[] = "/tmp/chibi-player-url-XXXXXX";
static char driver_path[1024], log_path[1024];
static yyjson_doc *result;

/* Extract the production EM_JS bodies. Only browser events, activation, clocks
 * and window.open are replaced; C owns each scenario and its assertions. */
static const char *driver =
    "const vm=require('node:vm'),fs=require('node:fs');\n"
    "const source=fs.readFileSync(process.argv[2],'utf8');\n"
    "let time=0,nextTimer=0;const timers=new Map(),opened=[],states={};\n"
    "const target=()=>{const listeners=[];return {\n"
    " addEventListener(type,callback,capture=false){if(!listeners.some(x=>"
    "x.type===type&&x.callback===callback&&x.capture===capture))"
    "listeners.push({type,callback,capture});},\n"
    " removeEventListener(type,callback,capture=false){const i=listeners."
    "findIndex(x=>x.type===type&&x.callback===callback&&x.capture===capture);"
    "if(i>=0)listeners.splice(i,1);},\n"
    " emit(type,details={}){const event={type,isTrusted:true,repeat:false,"
    "...details};for(const listener of [...listeners])"
    "if(listener.type===type)listener.callback(event);},\n"
    " count:()=>listeners.length};};\n"
    "const window=target(),document=Object.assign(target(),{hidden:false}),"
    "activation={isActive:false},strings=new Map();\n"
    "window.open=(...args)=>{opened.push(args);return null;};\n"
    "const context={Module:{},window,document,"
    "navigator:{userActivation:activation},performance:{now:()=>time},\n"
    " UTF8ToString:pointer=>strings.get(pointer),\n"
    " setTimeout(callback,delay){timers.set(++nextTimer,{at:time+delay,"
    "callback});return nextTimer;},clearTimeout:id=>timers.delete(id)};\n"
    "vm.createContext(context);let extracted=0;\n"
    "const bodies=/EM_JS\\(void, (player_url_\\w+), \\([^)]*\\), "
    "\\{([\\s\\S]*?)^\\}\\)/gm;\n"
    "for(const match of source.matchAll(bodies)){vm.runInContext('function '+"
    "match[1]+'(url){'+match[2]+'}\\n',context);extracted++;}\n"
    "if(extracted!==3)throw Error('Expected all three player URL functions');\n"
    "const start=()=>context.player_url_start(),"
    "stop=()=>context.player_url_stop();\n"
    "const "
    "submit=url=>{strings.set(1,url);context.player_url_submit(url?1:0);};\n"
    "const advance=ms=>{time+=ms;for(const [id,timer] of [...timers])"
    "if(timer.at<=time){timers.delete(id);timer.callback();}};\n"
    "const snapshot=name=>{states[name]={opens:opened.length,"
    "timers:timers.size,listeners:window.count()+document.count(),"
    "running:!!context.Module.chibiUrlHost,"
    "delay:timers.size?[...timers.values()][0].at-time:0};};\n";

static void setup()
{
    strcpy(directory, "/tmp/chibi-player-url-XXXXXX");
    assert(mkdtemp(directory));
    snprintf(driver_path, sizeof driver_path, "%s/driver.cjs", directory);
    snprintf(log_path, sizeof log_path, "%s/output.json", directory);
}

static void teardown()
{
    yyjson_doc_free(result);
    result = NULL;
    tool_remove_tree(directory);
}

static bool run(const char *scenario)
{
    FILE *file = fopen(driver_path, "w");
    assert(file);
    assert(fputs(driver, file) >= 0);
    assert(fputs(scenario, file) >= 0);
    assert(fputs("\nconsole.log(JSON.stringify({states,opened}));\n", file) >= 0);
    assert(!fclose(file));
    char source[1024];
    snprintf(source, sizeof source, "%s/src/host/player_url.c", tool_root());
    const char *argv[] = { "node", driver_path, source, NULL };
    if (tool_run(argv, tool_root(), log_path)) {
        return false;
    }
    char *text = tool_read(log_path);
    result = yyjson_read(text, strlen(text), 0);
    free(text);
    return result != NULL;
}

static yyjson_val *state(const char *name, const char *key)
{
    yyjson_val *states = yyjson_obj_get(yyjson_doc_get_root(result), "states");
    yyjson_val *value = yyjson_obj_get(yyjson_obj_get(states, name), key);
    assert(value);
    return value;
}

static int number(const char *name, const char *key)
{
    yyjson_val *value = state(name, key);
    assert(yyjson_is_int(value));
    return yyjson_get_int(value);
}

static const char *opened(int index, int argument)
{
    yyjson_val *calls = yyjson_obj_get(yyjson_doc_get_root(result), "opened");
    yyjson_val *call = yyjson_arr_get(calls, (size_t)index);
    const char *value = yyjson_get_str(yyjson_arr_get(call, (size_t)argument));
    assert(value);
    return value;
}

TEST_CASE(quick_keyboard_release_can_launch_from_the_authored_frame_once)
{
    REQUIRE(
        run("start();activation.isActive=true;"
            "window.emit('keydown',{code:'KeyE',key:'e'});"
            "window.emit('keyup',{code:'KeyE',key:'e'});time=32;"
            "submit('https://example.invalid/first');snapshot('frame');"
            "submit('https://example.invalid/second');"
            "window.emit('keyup',{code:'KeyE',key:'e'});snapshot('duplicate');")
    );
    REQUIRE(number("frame", "opens") == 1);
    REQUIRE(number("frame", "timers") == 0);
    REQUIRE(number("duplicate", "opens") == 1);
    REQUIRE(!strcmp(opened(0, 0), "https://example.invalid/first"));
    REQUIRE(!strcmp(opened(0, 1), "_blank"));
    REQUIRE(!strcmp(opened(0, 2), "noopener,noreferrer"));
    return true;
}

TEST_CASE(quick_pointer_release_can_launch_from_the_authored_frame)
{
    REQUIRE(run(
        "start();window.emit('pointerdown',{pointerId:7});"
        "activation.isActive=true;window.emit('pointerup',{pointerId:7});"
        "time=32;submit('https://example.invalid/pointer');snapshot('frame');"
    ));
    REQUIRE(number("frame", "opens") == 1);
    REQUIRE(number("frame", "timers") == 0);
    REQUIRE(!strcmp(opened(0, 0), "https://example.invalid/pointer"));
    return true;
}

TEST_CASE(touch_request_waits_for_its_matching_release_and_owns_the_url)
{
    REQUIRE(
        run("start();window.emit('pointerdown',{pointerId:7});time=120;"
            "submit('https://example.invalid/touch');snapshot('waiting');"
            "strings.set(1,'https://example.invalid/reused-memory');"
            "activation.isActive=true;window.emit('pointerup',{pointerId:8});"
            "snapshot('unrelated');window.emit('pointerup',{pointerId:7});"
            "snapshot('released');window.emit('pointerup',{pointerId:7});"
            "submit('https://example.invalid/repeated');snapshot('duplicate');")
    );
    REQUIRE(number("waiting", "opens") == 0);
    REQUIRE(number("waiting", "timers") == 1);
    REQUIRE(number("waiting", "delay") == 4880);
    REQUIRE(number("unrelated", "opens") == 0);
    REQUIRE(number("unrelated", "timers") == 1);
    REQUIRE(number("released", "opens") == 1);
    REQUIRE(number("released", "timers") == 0);
    REQUIRE(number("duplicate", "opens") == 1);
    REQUIRE(!strcmp(opened(0, 0), "https://example.invalid/touch"));
    return true;
}

TEST_CASE(a_new_gesture_cancels_the_previous_request)
{
    REQUIRE(
        run("start();window.emit('pointerdown',{pointerId:1});"
            "submit('https://example.invalid/old');activation.isActive=true;"
            "window.emit('keydown',{code:'KeyE',key:'e'});snapshot('new');"
            "window.emit('pointerup',{pointerId:1});"
            "window.emit('keyup',{code:'KeyE',key:'e'});snapshot('released');"
            "submit('https://example.invalid/new');snapshot('submitted');")
    );
    REQUIRE(number("new", "opens") == 0);
    REQUIRE(number("new", "timers") == 0);
    REQUIRE(number("released", "opens") == 0);
    REQUIRE(number("submitted", "opens") == 1);
    REQUIRE(!strcmp(opened(0, 0), "https://example.invalid/new"));
    return true;
}

TEST_CASE(pause_escape_and_pointer_cancel_discard_waiting_requests)
{
    REQUIRE(run(
        "start();window.emit('pointerdown',{pointerId:1});"
        "submit('https://example.invalid/paused');submit(null);"
        "activation.isActive=true;window.emit('pointerup',{pointerId:1});"
        "snapshot('paused');activation.isActive=false;"
        "window.emit('pointerdown',{pointerId:2});"
        "submit('https://example.invalid/escaped');"
        "window.emit('keydown',{code:'Escape',key:'Escape'});"
        "activation.isActive=true;window.emit('pointerup',{pointerId:2});"
        "snapshot('escaped');activation.isActive=false;"
        "window.emit('pointerdown',{pointerId:3});"
        "submit('https://example.invalid/cancelled');"
        "window.emit('pointercancel',{pointerId:3});activation.isActive=true;"
        "window.emit('pointerup',{pointerId:3});snapshot('cancelled');"
    ));
    const char *states[] = { "paused", "escaped", "cancelled" };
    for (int i = 0; i < 3; i++) {
        REQUIRE(number(states[i], "opens") == 0);
        REQUIRE(number(states[i], "timers") == 0);
    }
    return true;
}

TEST_CASE(expired_gestures_cannot_launch_before_or_after_timer_delivery)
{
    REQUIRE(
        run("start();window.emit('pointerdown',{pointerId:1});time=5001;"
            "activation.isActive=true;submit('https://example.invalid/stale');"
            "snapshot('late-submit');activation.isActive=false;"
            "window.emit('pointerdown',{pointerId:2});"
            "submit('https://example.invalid/timer');advance(5001);"
            "activation.isActive=true;window.emit('pointerup',{pointerId:2});"
            "snapshot('timer');activation.isActive=false;"
            "window.emit('pointerdown',{pointerId:3});"
            "submit('https://example.invalid/delayed-timer');time+=5001;"
            "activation.isActive=true;window.emit('pointerup',{pointerId:3});"
            "snapshot('late-release');")
    );
    const char *states[] = { "late-submit", "timer", "late-release" };
    for (int i = 0; i < 3; i++) {
        REQUIRE(number(states[i], "opens") == 0);
        REQUIRE(number(states[i], "timers") == 0);
    }
    return true;
}

TEST_CASE(released_gestures_without_activation_cannot_wait_for_later_activation)
{
    REQUIRE(run(
        "start();window.emit('keydown',{code:'KeyE',key:'e'});"
        "window.emit('keyup',{code:'KeyE',key:'e'});"
        "submit('https://example.invalid/inactive');snapshot('inactive');"
        "activation.isActive=true;window.emit('keyup',{code:'KeyE',key:'e'});"
        "submit('https://example.invalid/later');snapshot('later');"
    ));
    REQUIRE(number("inactive", "opens") == 0);
    REQUIRE(number("inactive", "timers") == 0);
    REQUIRE(number("later", "opens") == 0);
    REQUIRE(number("later", "timers") == 0);
    return true;
}

TEST_CASE(hidden_pages_discard_requests_and_reject_launches)
{
    REQUIRE(run(
        "start();window.emit('pointerdown',{pointerId:1});"
        "submit('https://example.invalid/hidden');document.hidden=true;"
        "document.emit('visibilitychange');document.hidden=false;"
        "activation.isActive=true;window.emit('pointerup',{pointerId:1});"
        "snapshot('visibility');activation.isActive=false;"
        "window.emit('pointerdown',{pointerId:2});"
        "submit('https://example.invalid/pagehide');window.emit('pagehide');"
        "activation.isActive=true;window.emit('pointerup',{pointerId:2});"
        "snapshot('pagehide');window.emit('pointerdown',{pointerId:3});"
        "document.hidden=true;submit('https://example.invalid/already-hidden');"
        "snapshot('hidden-submit');"
    ));
    const char *states[] = { "visibility", "pagehide", "hidden-submit" };
    for (int i = 0; i < 3; i++) {
        REQUIRE(number(states[i], "opens") == 0);
        REQUIRE(number(states[i], "timers") == 0);
    }
    return true;
}

TEST_CASE(stop_removes_listeners_and_timers_and_restart_has_no_stale_request)
{
    REQUIRE(run(
        "start();start();snapshot('started');"
        "window.emit('pointerdown',{pointerId:1});"
        "submit('https://example.invalid/stopped');snapshot('waiting');"
        "stop();stop();activation.isActive=true;"
        "window.emit('pointerup',{pointerId:1});advance(5001);"
        "submit('https://example.invalid/after-stop');snapshot('stopped');"
        "start();window.emit('pointerup',{pointerId:1});snapshot('restarted');"
        "window.emit('pointerdown',{pointerId:2});"
        "submit('https://example.invalid/restarted');snapshot('fresh');"
        "stop();snapshot('finished');"
    ));
    REQUIRE(number("started", "listeners") == 7);
    REQUIRE(number("waiting", "timers") == 1);
    REQUIRE(number("stopped", "opens") == 0);
    REQUIRE(number("stopped", "timers") == 0);
    REQUIRE(number("stopped", "listeners") == 0);
    REQUIRE(!yyjson_get_bool(state("stopped", "running")));
    REQUIRE(number("restarted", "opens") == 0);
    REQUIRE(number("restarted", "listeners") == 7);
    REQUIRE(yyjson_get_bool(state("restarted", "running")));
    REQUIRE(number("fresh", "opens") == 1);
    REQUIRE(!strcmp(opened(0, 0), "https://example.invalid/restarted"));
    REQUIRE(number("finished", "timers") == 0);
    REQUIRE(number("finished", "listeners") == 0);
    return true;
}

void test_tool_player_url()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(quick_keyboard_release_can_launch_from_the_authored_frame_once);
    RUN_TEST_CASE(quick_pointer_release_can_launch_from_the_authored_frame);
    RUN_TEST_CASE(touch_request_waits_for_its_matching_release_and_owns_the_url);
    RUN_TEST_CASE(a_new_gesture_cancels_the_previous_request);
    RUN_TEST_CASE(pause_escape_and_pointer_cancel_discard_waiting_requests);
    RUN_TEST_CASE(expired_gestures_cannot_launch_before_or_after_timer_delivery);
    RUN_TEST_CASE(released_gestures_without_activation_cannot_wait_for_later_activation);
    RUN_TEST_CASE(hidden_pages_discard_requests_and_reject_launches);
    RUN_TEST_CASE(stop_removes_listeners_and_timers_and_restart_has_no_stale_request);
    pu_setup(NULL, NULL);
}
