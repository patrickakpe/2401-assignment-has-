#include <stdio.h>

#include "defs.h"

/* A starting point: change this file however you like. */
int main(int argc, char* argv[])
{
    struct Config config;
    if (tools_parse_args(argc, argv, &config) != C_OK) return 1;

    // TODO: load the building, then the crew, then place the ghost.
    // TODO: run the drill: sequential, or threaded with --threaded.
    // TODO: print the results.
    // TODO: clean up the building.
    return 0;
}
