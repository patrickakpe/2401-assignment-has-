#define _POSIX_C_SOURCE 200809L
#include "tools.h"

#include <semaphore.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Provided code keeps a little state here. Your code must not use globals. */
static sem_t rand_lock;
static unsigned rand_state;
static unsigned tick_usec;
static bool watch;
static _Atomic uint64_t next_seq;

static const enum GhostType ghost_list[] = {
    GH_POLTERGEIST, GH_NAPPA,  GH_WISP,  GH_BANSHEE, GH_BULLIES,
    GH_PHANTOM,     GH_WRAITH, GH_SHADE, GH_PERONA,  GH_SPIRIT,
};
#define GHOST_COUNT ((int)(sizeof(ghost_list) / sizeof(ghost_list[0])))

/* ---- Evidence and ghosts ---- */

const char *evidence_to_string(enum EvidenceType evidence)
{
    switch (evidence) {
    case EV_EMF:
        return "EMF";
    case EV_RADIO:
        return "Radio";
    case EV_TEMPERATURE:
        return "Temperature";
    case EV_FINGERPRINTS:
        return "Fingerprints";
    case EV_WRITING:
        return "Writing";
    default:
        return "None";
    }
}

const char *ghost_to_string(enum GhostType ghost)
{
    switch (ghost) {
    case GH_POLTERGEIST:
        return "Poltergeist";
    case GH_NAPPA:
        return "Nappa";
    case GH_WISP:
        return "Wisp";
    case GH_BANSHEE:
        return "Banshee";
    case GH_BULLIES:
        return "Bullies";
    case GH_PHANTOM:
        return "Phantom";
    case GH_WRAITH:
        return "Wraith";
    case GH_SHADE:
        return "Shade";
    case GH_PERONA:
        return "Perona";
    case GH_SPIRIT:
        return "Spirit";
    default:
        return "Unknown";
    }
}

const char *exit_reason_to_string(enum ExitReason reason)
{
    switch (reason) {
    case EXIT_EVIDENCE:
        return "solved the case";
    case EXIT_AFRAID:
        return "too afraid";
    case EXIT_DAWN:
        return "dawn";
    default:
        return "still hunting";
    }
}

int evidence_index(enum EvidenceType evidence)
{
    for (int i = 0; i < EVIDENCE_TYPE_COUNT; i++) {
        if ((int)evidence == (1 << i))
            return i;
    }
    return -1;
}

enum EvidenceType evidence_from_index(int index)
{
    if (index < 0 || index >= EVIDENCE_TYPE_COUNT)
        return EV_NONE;
    return (enum EvidenceType)(1 << index);
}

bool evidence_identifies_ghost(EvidenceByte bits, enum GhostType *ghost)
{
    int count = 0;
    for (int i = 0; i < 8; i++) {
        if (bits & (1 << i))
            count++;
    }
    bool valid = count == 3 && (bits >> EVIDENCE_TYPE_COUNT) == 0;
    if (valid && ghost != NULL)
        *ghost = (enum GhostType)bits;
    return valid;
}

enum GhostType ghost_random(void)
{
    return ghost_list[tools_rand_int(0, GHOST_COUNT)];
}

/* ---- Configuration ---- */

static void delete_old_recordings(void)
{
    char path[32];
    remove("rec-g0.bin");
    for (int i = 0;; i++) {
        snprintf(path, sizeof(path), "rec-h%d.bin", i);
        if (remove(path) != 0)
            break;
    }
}

int tools_parse_args(int argc, char *argv[], struct Config *config)
{
    config->building = NULL;
    config->crew = NULL;
    config->threaded = false;
    config->seed = (unsigned)time(NULL);
    config->tick_usec = 0;
    config->fear_max = DEFAULT_FEAR_MAX;
    config->watch = false;

    int positional = 0;
    for (int i = 1; i < argc; i++) {
        bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--threaded") == 0) {
            config->threaded = true;
        } else if (strcmp(argv[i], "--watch") == 0) {
            config->watch = true;
        } else if (strcmp(argv[i], "--fear") == 0 && has_value) {
            config->fear_max = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && has_value) {
            config->seed = (unsigned)atol(argv[++i]);
        } else if (strcmp(argv[i], "--tick") == 0 && has_value) {
            config->tick_usec = (unsigned)atol(argv[++i]);
        } else if (strncmp(argv[i], "--", 2) == 0) {
            // A flag we don't know: left for your own extensions.
        } else if (positional == 0) {
            config->building = argv[i];
            positional++;
        } else if (positional == 1) {
            config->crew = argv[i];
            positional++;
        }
    }

    if (config->building == NULL || config->crew == NULL) {
        fprintf(stderr,
                "usage: %s <building.bldg> <crew.crew> [--threaded] [--seed N] [--tick USEC] "
                "[--fear N] [--watch]\n",
                argv[0]);
        return C_ERR_BAD_ARG;
    }
    if (config->threaded && config->tick_usec < MIN_THREADED_TICK_US) {
        config->tick_usec = MIN_THREADED_TICK_US;
    }

    delete_old_recordings();
    sem_init(&rand_lock, 0, 1);
    rand_state = config->seed;
    tick_usec = config->tick_usec;
    watch = config->watch;

    printf("Config: %s %s threaded=%s seed=%u tick=%u fear=%d\n", config->building, config->crew,
           config->threaded ? "yes" : "no", config->seed, config->tick_usec, config->fear_max);
    return C_OK;
}

int tools_rand_int(int lower, int upper)
{
    if (upper <= lower)
        return lower;
    sem_wait(&rand_lock);
    int value = rand_r(&rand_state);
    sem_post(&rand_lock);
    return lower + value % (upper - lower);
}

/* ---- Recording ---- */

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int recorder_open(struct Recorder *rec, enum EntityKind kind, int entity)
{
    char path[32];
    snprintf(path, sizeof(path), "rec-%c%d.bin", kind == ENTITY_GHOST ? 'g' : 'h', entity);
    rec->file = fopen(path, "wb");
    rec->kind = (uint8_t)kind;
    rec->entity = (uint16_t)entity;
    rec->count = 0;
    if (rec->file == NULL) {
        perror(path);
        return C_ERR_IO;
    }
    return C_OK;
}

void recorder_close(struct Recorder *rec)
{
    if (rec->file != NULL)
        fclose(rec->file);
    rec->file = NULL;
}

static void write_record(struct Recorder *rec, struct Record *record)
{
    if (rec->file == NULL)
        return;
    if (rec->count == RECORDER_MAX_RECORDS) {
        fprintf(stderr, "recorder: %s %d reached %d records; recording stopped (a runaway loop?)\n",
                rec->kind == ENTITY_GHOST ? "ghost" : "hunter", rec->entity, RECORDER_MAX_RECORDS);
    }
    if (rec->count++ >= RECORDER_MAX_RECORDS)
        return;
    fwrite(record, sizeof(*record), 1, rec->file);
}

/* --watch: one line per record. Threaded, lines can print slightly out of order. */
static void watch_record(const struct Record *record)
{
    static const char *names[] = { "INIT",  "LEAVE",  "ENTER",    "HAUNT",  "READ",
                                   "FOUND", "REPORT", "CHECKOUT", "RETURN", "EXIT" };
    printf("%c%-2u %-8s room %u", record->kind == ENTITY_GHOST ? 'g' : 'h', record->entity,
           names[record->event], record->room);

    int arg = record->arg;
    bool ghost = record->kind == ENTITY_GHOST;
    switch (record->event) {
    case REC_INIT:
        if (ghost)
            printf(" %s", ghost_to_string(arg));
        break;
    case REC_ENTER:
        if (arg)
            printf(" (following the trail)");
        break;
    case REC_EXIT:
        if (!ghost)
            printf(" %s", exit_reason_to_string(arg));
        break;
    case REC_LEAVE:
        break;
    default:
        for (int i = 0; i < EVIDENCE_TYPE_COUNT; i++) {
            enum EvidenceType type = evidence_from_index(i);
            if (arg & type)
                printf(" %s", evidence_to_string(type));
        }
    }
    printf("\n");
}

void record_event(struct Recorder *rec, enum RecordEvent event, int room, int arg)
{
    struct Record record = { atomic_fetch_add(&next_seq, 1),
                             now_ns(),
                             0,
                             rec->entity,
                             (uint16_t)room,
                             rec->kind,
                             (uint8_t)event,
                             (uint8_t)arg,
                             0 };
    if (watch)
        watch_record(&record);
    write_record(rec, &record);
}

void record_end_turn(struct Recorder *rec)
{
    struct Record record = {
        atomic_fetch_add(&next_seq, 1), now_ns(), 0, rec->entity, 0, rec->kind, REC_TURN, 0, 0
    };
    if (tick_usec > 0) {
        struct timespec ts = { tick_usec / 1000000, (long)(tick_usec % 1000000) * 1000 };
        nanosleep(&ts, NULL);
    }
    record.t_end = now_ns();
    write_record(rec, &record);
}
