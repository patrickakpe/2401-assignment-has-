/* Tester: checks the recordings your program left in the current directory.
 *
 *   ./tester <building.bldg> [--threaded]
 *
 * Run it right after your program, with the same building. Each line names the
 * requirement it checks. NONE means nothing happened that the check could look
 * at, so try another --seed, or --fear 1000 so nobody is scared off early.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools.h"

#define NAME_LEN    24
#define MAX_SAMPLES 3

/* ---- The building, read straight from the file ---- */

struct TestBuilding {
    int  room_count;
    int  entry;
    int  devices[EVIDENCE_TYPE_COUNT];
    char names[BLDG_MAX_ROOMS][NAME_LEN];
    bool connected[BLDG_MAX_ROOMS][BLDG_MAX_ROOMS];
};

static int load_building(struct TestBuilding* b, const char* path)
{
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        return -1;
    }

    unsigned char header[16];
    bool ok = fread(header, 16, 1, file) == 1 && strncmp((char*)header, "BLDG", 4) == 0;
    b->entry = header[5];
    b->room_count = header[6] | (header[7] << 8);
    ok = ok && b->room_count >= 1 && b->room_count <= BLDG_MAX_ROOMS;
    for (int i = 0; i < EVIDENCE_TYPE_COUNT; i++) b->devices[i] = header[8 + i];

    for (int i = 0; ok && i < b->room_count; i++) {
        unsigned char mask[4];
        ok = fread(b->names[i], NAME_LEN, 1, file) == 1 && fread(mask, 4, 1, file) == 1;
        b->names[i][NAME_LEN - 1] = '\0';
        uint32_t connections = mask[0] | (mask[1] << 8) | (mask[2] << 16) | ((uint32_t)mask[3] << 24);
        for (int j = 0; j < BLDG_MAX_ROOMS; j++) b->connected[i][j] = (connections >> j) & 1;
    }
    fclose(file);
    return ok ? 0 : -1;
}

/* ---- One entity's recording ---- */

struct Recording {
    struct Record* records;
    int            count;
};

static int load_recording(struct Recording* r, const char* path)
{
    FILE* file = fopen(path, "rb");
    if (file == NULL) return -1;

    int capacity = 256;
    r->records = malloc(capacity * sizeof(struct Record));
    r->count = 0;
    struct Record record;
    while (fread(&record, sizeof(record), 1, file) == 1) {
        if (r->count == capacity) {
            capacity *= 2;
            struct Record* bigger = malloc(capacity * sizeof(struct Record));
            for (int i = 0; i < r->count; i++) bigger[i] = r->records[i];
            free(r->records);
            r->records = bigger;
        }
        r->records[r->count] = record;
        r->count++;
    }
    fclose(file);
    return 0;
}

/* ---- Check results ---- */

struct Check {
    const char* id;
    const char* description;
    int         checked;
    int         failed;
    char        samples[MAX_SAMPLES][160];
};

enum {
    C_G1, C_G2, C_G3, C_G4, C_G5, C_G6,
    C_H1, C_H2, C_H3, C_H6, C_H7, C_H8, C_H9, C_H10, C_H11, CHECK_COUNT
};

/* Each check names the spec requirement it tests. */
static struct Check checks[CHECK_COUNT] = {
    { "G1", "The ghost starts once, outside the entry", 0, 0, { "" } },
    { "G2", "The ghost moves between connected rooms", 0, 0, { "" } },
    { "G3", "The ghost haunts with its own evidence types", 0, 0, { "" } },
    { "G4", "The ghost never leaves a room with a hunter in it", 0, 0, { "" } },
    { "G5", "The ghost exits once, as its last event", 0, 0, { "" } },
    { "G6", "The ghost moves at most once per turn", 0, 0, { "" } },
    { "H1", "Each hunter starts once, at the entry", 0, 0, { "" } },
    { "H2", "Hunters move between connected rooms, never in two", 0, 0, { "" } },
    { "H3/H4", "After a find, hunters retrace the trail, which clears at the entry", 0, 0, { "" } },
    { "H6", "Devices are used while held, finding only what's there", 0, 0, { "" } },
    { "H7", "Reports happen at the van and contain found evidence", 0, 0, { "" } },
    { "H8", "Devices are taken and returned at the van, within supply", 0, 0, { "" } },
    { "H9", "Hunters who solved the case match the actual ghost", 0, 0, { "" } },
    { "H10", "Each hunter exits once, as its last event", 0, 0, { "" } },
    { "H11", "Hunters move at most once per turn", 0, 0, { "" } },
};

static void pass_or_fail(int check, bool ok, const char* who, int index, const char* detail)
{
    struct Check* c = &checks[check];
    c->checked++;
    if (ok) return;
    if (c->failed < MAX_SAMPLES) snprintf(c->samples[c->failed], 160, "%s %d: %s", who, index, detail);
    c->failed++;
}

#define TWO_MOVES "moved twice in one turn: is a record_end_turn() call missing?"

/* ---- Each entity on its own: its recording is in the order it did things ---- */

static void check_hunter(const struct TestBuilding* b, const struct Recording* r, int h)
{
    char msg[120];
    int room = b->entry; // assumed if INIT is missing, so the rest still gets checked
    int last_room = -1;
    int device = EV_NONE, found = 0;
    bool heading_back = false; // found evidence, not reported yet
    int moves_this_turn = 0;
    bool exited = false;
    int trail[100000];
    int depth = 0;

    if (r->count == 0 || r->records[0].event != REC_INIT) {
        pass_or_fail(C_H1, false, "hunter", h, "no INIT recorded first; assuming the entry");
    }

    for (int i = 0; i < r->count; i++) {
        const struct Record* e = &r->records[i];
        if (exited) {
            pass_or_fail(C_H10, false, "hunter", h, "recorded something after exiting");
            break;
        }
        switch (e->event) {
            case REC_INIT:
                pass_or_fail(C_H1, i == 0 && e->room == b->entry, "hunter", h, "INIT not first, or not at the entry");
                room = e->room;
                break;
            case REC_LEAVE:
                snprintf(msg, sizeof(msg), "left %s without being in it", b->names[e->room]);
                pass_or_fail(C_H2, room == e->room, "hunter", h, msg);
                moves_this_turn++;
                if (moves_this_turn > 1) pass_or_fail(C_H11, false, "hunter", h, TWO_MOVES);
                last_room = e->room;
                room = -1;
                break;
            case REC_ENTER:
                pass_or_fail(C_H2, room == -1, "hunter", h, "entered a room without leaving the last one");
                snprintf(msg, sizeof(msg), "moved %s -> %s, which aren't connected",
                         last_room < 0 ? "?" : b->names[last_room], b->names[e->room]);
                pass_or_fail(C_H2, last_room >= 0 && b->connected[last_room][e->room], "hunter", h, msg);
                if (heading_back) {
                    snprintf(msg, sizeof(msg), "found evidence, then moved to %s instead of back along the trail",
                             b->names[e->room]);
                    pass_or_fail(C_H3, e->arg, "hunter", h, msg);
                }
                if (e->arg) {
                    snprintf(msg, sizeof(msg), "went back to %s, but the trail says %s", b->names[e->room],
                             depth > 0 ? b->names[trail[depth - 1]] : "(empty)");
                    pass_or_fail(C_H3, depth > 0 && trail[depth - 1] == e->room, "hunter", h, msg);
                    if (depth > 0) depth--;
                } else if (last_room >= 0 && depth < 100000) {
                    trail[depth] = last_room;
                    depth++;
                }
                if (e->room == b->entry) {
                    depth = 0; // the van clears the trail
                    heading_back = false;
                }
                room = e->room;
                break;
            case REC_CHECKOUT:
                pass_or_fail(C_H8, room == b->entry && device == EV_NONE, "hunter", h,
                             "checked out a device away from the van, or while holding one");
                device = e->arg;
                break;
            case REC_RETURN:
                pass_or_fail(C_H8, room == b->entry && device == e->arg, "hunter", h,
                             "returned a device it wasn't holding, or away from the van");
                device = EV_NONE;
                break;
            case REC_READ:
                pass_or_fail(C_H6, device != EV_NONE && e->arg == device, "hunter", h,
                             "used a device it wasn't holding");
                break;
            case REC_FOUND:
                pass_or_fail(C_H6, e->arg == device, "hunter", h, "found evidence its device can't detect");
                found |= e->arg;
                if (room != b->entry) heading_back = true;
                break;
            case REC_REPORT:
                pass_or_fail(C_H7, room == b->entry, "hunter", h, "reported away from the van");
                pass_or_fail(C_H7, (e->arg & ~found) == 0, "hunter", h, "reported evidence it never found");
                found &= ~e->arg;
                heading_back = false;
                break;
            case REC_EXIT:
                pass_or_fail(C_H2, room != -1, "hunter", h,
                             "left a room, then exited: EXIT already takes it out of the room");
                pass_or_fail(C_H10, true, "hunter", h, "");
                exited = true;
                break;
            case REC_TURN:
                pass_or_fail(C_H11, true, "hunter", h, "");
                moves_this_turn = 0;
                break;
        }
    }
    if (!exited) pass_or_fail(C_H10, false, "hunter", h, "never exited");
}

static void check_ghost(const struct TestBuilding* b, const struct Recording* r)
{
    char msg[120];
    int room = -1, last_room = -1;
    int ghost_type = 0;
    int moves_this_turn = 0;
    bool exited = false;

    if (r->count == 0 || r->records[0].event != REC_INIT) {
        pass_or_fail(C_G1, false, "ghost", 0, "no INIT recorded first");
    }

    for (int i = 0; i < r->count; i++) {
        const struct Record* e = &r->records[i];
        if (exited) {
            pass_or_fail(C_G5, false, "ghost", 0, "recorded something after exiting");
            break;
        }
        switch (e->event) {
            case REC_INIT:
                pass_or_fail(C_G1, i == 0 && (b->room_count == 1 || e->room != b->entry), "ghost", 0,
                             "INIT not first, or started at the entry");
                pass_or_fail(C_G1, evidence_identifies_ghost(e->arg, NULL), "ghost", 0,
                             "INIT's arg isn't a ghost type");
                ghost_type = e->arg;
                room = e->room;
                break;
            case REC_LEAVE:
                pass_or_fail(C_G2, room == e->room, "ghost", 0, "left a room it wasn't in");
                moves_this_turn++;
                if (moves_this_turn > 1) pass_or_fail(C_G6, false, "ghost", 0, TWO_MOVES);
                last_room = e->room;
                room = -1;
                break;
            case REC_ENTER:
                snprintf(msg, sizeof(msg), "moved %s -> %s, which aren't connected",
                         last_room < 0 ? "?" : b->names[last_room], b->names[e->room]);
                pass_or_fail(C_G2, last_room >= 0 && b->connected[last_room][e->room], "ghost", 0, msg);
                room = e->room;
                break;
            case REC_HAUNT:
                snprintf(msg, sizeof(msg), "left %s, which a %s doesn't leave", evidence_to_string(e->arg),
                         ghost_to_string(ghost_type));
                pass_or_fail(C_G3, evidence_index(e->arg) >= 0 && (e->arg & ghost_type) != 0, "ghost", 0, msg);
                pass_or_fail(C_G3, e->room == room, "ghost", 0, "haunted a room it wasn't in");
                break;
            case REC_EXIT:
                pass_or_fail(C_G5, true, "ghost", 0, "");
                exited = true;
                break;
            case REC_TURN:
                pass_or_fail(C_G6, true, "ghost", 0, "");
                moves_this_turn = 0;
                break;
        }
    }
    if (!exited) pass_or_fail(C_G5, false, "ghost", 0, "never exited");
}

/* ---- Everyone together, in the order things happened ----
 * Each event's sequence number is taken inside the critical section, so events
 * in the same room, on the case board or at the van are in their true order. */

static int next_by_seq(struct Recording* all, int* next, int count)
{
    int best = -1;
    for (int i = 0; i < count; i++) {
        if (next[i] >= all[i].count) continue;
        if (best < 0 || all[i].records[next[i]].seq < all[best].records[next[best]].seq) best = i;
    }
    return best;
}

static void check_together(const struct TestBuilding* b, struct Recording* all, int count)
{
    char msg[120];
    unsigned char evidence[BLDG_MAX_ROOMS] = { 0 };
    int hunters_in[BLDG_MAX_ROOMS] = { 0 };
    int checked_out[EVIDENCE_TYPE_COUNT] = { 0 };
    int ghost_type = 0, case_board = 0;
    int* next = calloc(count, sizeof(int));

    for (int s = next_by_seq(all, next, count); s >= 0; s = next_by_seq(all, next, count)) {
        const struct Record* e = &all[s].records[next[s]];
        next[s]++;
        bool hunter = e->kind == ENTITY_HUNTER;

        switch (e->event) {
            case REC_INIT:
                if (hunter) hunters_in[e->room]++;
                else ghost_type = e->arg;
                break;
            case REC_ENTER:
                if (hunter) hunters_in[e->room]++;
                break;
            case REC_LEAVE:
                if (hunter) {
                    hunters_in[e->room]--;
                } else {
                    snprintf(msg, sizeof(msg), "left %s with a hunter in it", b->names[e->room]);
                    pass_or_fail(C_G4, hunters_in[e->room] == 0, "ghost", 0, msg);
                }
                break;
            case REC_HAUNT:
                evidence[e->room] |= e->arg;
                break;
            case REC_FOUND:
                snprintf(msg, sizeof(msg), "found %s in %s before the ghost left it",
                         evidence_to_string(e->arg), b->names[e->room]);
                pass_or_fail(C_H6, (evidence[e->room] & e->arg) != 0, "hunter", e->entity, msg);
                break;
            case REC_REPORT:
                case_board |= e->arg;
                break;
            case REC_CHECKOUT: {
                int d = evidence_index(e->arg);
                if (d >= 0) checked_out[d]++;
                snprintf(msg, sizeof(msg), "checked out more %s devices than the van has", evidence_to_string(e->arg));
                pass_or_fail(C_H8, d >= 0 && checked_out[d] <= b->devices[d], "hunter", e->entity, msg);
                break;
            }
            case REC_RETURN: {
                int d = evidence_index(e->arg);
                if (d >= 0) checked_out[d]--;
                break;
            }
            case REC_EXIT:
                if (hunter) {
                    hunters_in[e->room]--;
                    if (e->arg == EXIT_EVIDENCE) {
                        pass_or_fail(C_H9, case_board == ghost_type, "hunter", e->entity,
                                     "left as solved, but the case board doesn't match the ghost");
                    }
                }
                break;
        }
    }
    free(next);
}

/* ---- Overlap: how many hunters were using a device at the same moment ----
 * A turn counts if the hunter recorded a READ during it. */

static int peak_device_overlap(struct Recording* hunters, int count)
{
    int total = 0;
    for (int h = 0; h < count; h++) total += hunters[h].count;
    uint64_t* begin = malloc((total + 1) * sizeof(uint64_t));
    uint64_t* end = malloc((total + 1) * sizeof(uint64_t));
    int n = 0;

    for (int h = 0; h < count; h++) {
        bool read_this_turn = false;
        for (int i = 0; i < hunters[h].count; i++) {
            const struct Record* e = &hunters[h].records[i];
            if (e->event == REC_READ) read_this_turn = true;
            if (e->event == REC_TURN) {
                if (read_this_turn) {
                    begin[n] = e->t_begin;
                    end[n] = e->t_end;
                    n++;
                }
                read_this_turn = false;
            }
        }
    }

    int peak = 0;
    for (int i = 0; i < n; i++) {
        int at_once = 0;
        for (int j = 0; j < n; j++) {
            if (begin[j] <= begin[i] && begin[i] < end[j]) at_once++;
        }
        if (at_once > peak) peak = at_once;
    }
    free(begin);
    free(end);
    return peak;
}

/* ---- Report ---- */

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <building.bldg> [--threaded]\n", argv[0]);
        return 2;
    }
    bool threaded = argc > 2 && strcmp(argv[2], "--threaded") == 0;

    struct TestBuilding building;
    if (load_building(&building, argv[1]) != 0) {
        fprintf(stderr, "%s: couldn't read the building\n", argv[1]);
        return 2;
    }

    // Recording 0 is the ghost; hunter h is recording h + 1.
    struct Recording all[1 + 4096];
    if (load_recording(&all[0], "rec-g0.bin") != 0) {
        fprintf(stderr, "no rec-g0.bin here: did your program run in this folder?\n");
        return 2;
    }
    int hunters = 0;
    char path[32];
    snprintf(path, sizeof(path), "rec-h%d.bin", hunters);
    while (hunters < 4096 && load_recording(&all[1 + hunters], path) == 0) {
        hunters++;
        snprintf(path, sizeof(path), "rec-h%d.bin", hunters);
    }

    check_ghost(&building, &all[0]);
    for (int h = 0; h < hunters; h++) check_hunter(&building, &all[1 + h], h);
    check_together(&building, all, 1 + hunters);
    int peak = peak_device_overlap(&all[1], hunters);

    int failures = 0;
    bool any_none = false;
    printf("Tested the ghost and %d hunters in %d rooms\n", hunters, building.room_count);
    for (int c = 0; c < CHECK_COUNT; c++) {
        struct Check* k = &checks[c];
        const char* status = k->failed > 0 ? "FAIL" : (k->checked == 0 ? "NONE" : "PASS");
        printf("[%s] %-5s %s (%d checked", status, k->id, k->description, k->checked);
        if (k->failed > 0) printf(", %d failed", k->failed);
        printf(")\n");
        for (int i = 0; i < k->failed && i < MAX_SAMPLES; i++) printf("         %s\n", k->samples[i]);
        failures += k->failed;
        if (k->checked == 0) any_none = true;
    }
    if (any_none) {
        printf("         NONE: nothing happened for that check to look at, so it wasn't tested.\n"
               "         Try another --seed, or --fear 1000 so hunters aren't scared off early.\n");
    }

    const char* overlap = !threaded ? "INFO" : (peak >= 2 ? "PASS" : "FAIL");
    printf("[%s] C5    Hunters use devices at the same time (peak: %d of %d)\n", overlap, peak, hunters);
    if (threaded && peak < 2) {
        printf("         Your hunters never used devices at the same time. Is a lock kept across a whole turn?\n");
        failures++;
    }

    for (int i = 0; i < 1 + hunters; i++) free(all[i].records);
    return failures > 0 ? 1 : 0;
}
