#ifndef DEFS_H
#define DEFS_H

#include "common.h"
#include "tools.h"

/*
    Your definitions, structures and function declarations go in this file.

    test_bldg.c needs struct Building and the three declarations below, so keep
    them, with these names and parameters. Everything else is yours to design.
*/

struct Building {
    // TODO: your fields
};

int  building_load(struct Building* building, const char* path);
void building_print(const struct Building* building);
void building_cleanup(struct Building* building);

#endif
