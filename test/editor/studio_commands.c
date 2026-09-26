#include "editor/studio/studio_internal.h"
#include "test/suite_internal.h"

#include <string.h>

/* Every command has an id that finds it again, so authored UI can run it by
 * emit('studio.<id>'), and no two share one. */
void test_studio_commands()
{
    bool found = true, unique = true;
    for (int c = 0; c < STUDIO_COMMAND_COUNT; c++) {
        found &= studio_command_named(studio_command_id((studio_command)c)) == c;
        for (int d = 0; d < c; d++) {
            unique &= strcmp(
                          studio_command_id((studio_command)c),
                          studio_command_id((studio_command)d)
                      ) != 0;
        }
    }
    test_check(found && unique && studio_command_named("missing") < 0, "each Studio command has its own id that names it");
}
