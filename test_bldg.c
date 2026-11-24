/* Loader test: loads a building and prints it with your building_print().
 * `make test-bldg` diffs the output against data/tests/<name>.expected.
 * A building that fails to load prints the status code's description instead.
 * Requires these three functions, as named in the specification:
 *   int  building_load(struct Building*, const char* path);
 *   void building_print(const struct Building*);
 *   void building_cleanup(struct Building*);
 */
#include <stdio.h>

#include "defs.h"

int main(int argc, char* argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <building.bldg>\n", argv[0]);
        return 2;
    }
    struct Building building;
    int rc = building_load(&building, argv[1]);
    if (rc != C_OK) {
        printf("Load failed: %s\n", c_strerror(rc));
        return 1;
    }
    building_print(&building);
    building_cleanup(&building);
    return 0;
}
