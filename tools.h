#ifndef TOOLS_H
#define TOOLS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "common.h"

/* ---- Evidence and ghosts ---- */

typedef unsigned char EvidenceByte;

enum EvidenceType {
    EV_NONE         = 0,
    EV_EMF          = 1 << 0,
    EV_RADIO        = 1 << 1,
    EV_TEMPERATURE  = 1 << 2,
    EV_FINGERPRINTS = 1 << 3,
    EV_WRITING      = 1 << 4,
};
#define EVIDENCE_TYPE_COUNT 5

/* Every combination of three evidence types is exactly one ghost. */
enum GhostType {
    GH_POLTERGEIST = EV_FINGERPRINTS | EV_TEMPERATURE | EV_WRITING,
    GH_NAPPA       = EV_FINGERPRINTS | EV_TEMPERATURE | EV_RADIO,
    GH_WISP        = EV_FINGERPRINTS | EV_TEMPERATURE | EV_EMF,
    GH_BANSHEE     = EV_FINGERPRINTS | EV_WRITING | EV_RADIO,
    GH_BULLIES     = EV_FINGERPRINTS | EV_WRITING | EV_EMF,
    GH_PHANTOM     = EV_FINGERPRINTS | EV_RADIO | EV_EMF,
    GH_WRAITH      = EV_TEMPERATURE | EV_WRITING | EV_RADIO,
    GH_SHADE       = EV_TEMPERATURE | EV_WRITING | EV_EMF,
    GH_PERONA      = EV_TEMPERATURE | EV_RADIO | EV_EMF,
    GH_SPIRIT      = EV_WRITING | EV_RADIO | EV_EMF,
};

enum ExitReason { EXIT_NONE = 0, EXIT_EVIDENCE = 1, EXIT_AFRAID = 2, EXIT_DAWN = 3 };

const char* evidence_to_string(enum EvidenceType evidence);
const char* ghost_to_string(enum GhostType ghost);
const char* exit_reason_to_string(enum ExitReason reason);

/**
 * Converts one evidence bit to its index, for arrays with one slot per type.
 * @param[in] evidence a single evidence bit
 * @return 0 to EVIDENCE_TYPE_COUNT - 1, or -1 if evidence is not exactly one bit
 */
int evidence_index(enum EvidenceType evidence);

/**
 * Converts an index back to its evidence bit.
 * @param[in] index 0 to EVIDENCE_TYPE_COUNT - 1
 * @return the evidence bit, or EV_NONE if index is out of range
 */
enum EvidenceType evidence_from_index(int index);

/**
 * Checks whether a set of evidence names a ghost (exactly three bits set).
 * @param[in]  bits  the evidence found so far
 * @param[out] ghost the ghost those bits name; may be NULL
 * @return true if bits names a ghost
 */
bool evidence_identifies_ghost(EvidenceByte bits, enum GhostType* ghost);

/** @return a random ghost type */
enum GhostType ghost_random(void);

/* ---- Building files ---- */

#define BLDG_MAX_ROOMS 32 // one bit per room in a uint32_t connection mask

/* ---- Configuration ---- */

#define DEFAULT_FEAR_MAX     7
#define MIN_THREADED_TICK_US 1000

struct Config {
    const char* building;  // path to the .bldg file
    const char* crew;      // path to the .crew file
    bool        threaded;  // --threaded: one thread per entity
    unsigned    seed;      // --seed N (default: based on the time)
    unsigned    tick_usec; // --tick N: microseconds each turn takes (default 0)
    int         fear_max;  // --fear N (default DEFAULT_FEAR_MAX)
    bool        watch;     // --watch: print each record as it's made
};

/**
 * Reads the command line: <building.bldg> <crew.crew> [--threaded] [--seed N] [--tick N] [--fear N] [--watch]
 * Flags it doesn't know are ignored, so you can add your own.
 *
 * Before returning, it also seeds the random numbers, sets the tick (at least
 * MIN_THREADED_TICK_US when threaded), deletes recordings left by an earlier run,
 * and prints the configuration on one line.
 *
 * @param[in]  argc, argv as passed to main()
 * @param[out] config     the configuration
 * @return C_OK, or C_ERR_BAD_ARG after printing the usage
 */
int tools_parse_args(int argc, char* argv[], struct Config* config);

/**
 * Thread-safe random integer.
 * @return a value in [lower, upper), or lower if upper <= lower
 */
int tools_rand_int(int lower, int upper);

/* ---- Recording ---- */

enum EntityKind { ENTITY_GHOST = 0, ENTITY_HUNTER = 1 };

enum RecordEvent {
    REC_INIT,     // placed at the start; arg: ghost type (ghost) or 0 (hunter)
    REC_LEAVE,    // left the room
    REC_ENTER,    // entered the room; arg: 1 if this move followed the trail back, else 0
    REC_HAUNT,    // ghost left evidence in the room; arg: the evidence bit
    REC_READ,     // hunter used its device in the room; arg: the device
    REC_FOUND,    // that reading found evidence; arg: the evidence bit
    REC_REPORT,   // hunter added evidence to the case board; arg: the evidence bits
    REC_CHECKOUT, // hunter took a device from the van; arg: the device
    REC_RETURN,   // hunter gave a device back to the van; arg: the device
    REC_EXIT,     // left the simulation; arg: ExitReason (hunter) or 0 (ghost)
    REC_TURN,     // written by record_end_turn()
};

struct Record {
    uint64_t seq;     // order across all entities, taken when the event is recorded
    uint64_t t_begin; // monotonic nanoseconds
    uint64_t t_end;   // REC_TURN only
    uint16_t entity;  // hunter index in crew order, 0 for the ghost
    uint16_t room;    // room index in .bldg order (unused for REC_TURN)
    uint8_t  kind;    // EntityKind
    uint8_t  event;   // RecordEvent
    uint8_t  arg;
    uint8_t  pad;
};

#define RECORDER_MAX_RECORDS 100000 // recording stops here, in case of a runaway loop

struct Recorder {
    FILE*    file;
    uint8_t  kind;
    uint16_t entity;
    long     count;
};

/**
 * Opens this entity's recording: rec-g0.bin for the ghost, rec-h<entity>.bin for a hunter.
 * @param[out] rec    the recorder to open
 * @param[in]  kind   ENTITY_GHOST or ENTITY_HUNTER
 * @param[in]  entity the hunter's index in crew order, or 0 for the ghost
 * @return C_OK, or C_ERR_IO if the file couldn't be opened
 */
int recorder_open(struct Recorder* rec, enum EntityKind kind, int entity);

/** Closes the recording. Safe to call twice. */
void recorder_close(struct Recorder* rec);

/**
 * Records one change to the simulation, while you make that change: inside the
 * room's critical section for LEAVE, ENTER, HAUNT, READ, FOUND and EXIT, inside
 * the case board's for REPORT, right after taking a device for CHECKOUT, and
 * right before giving it back for RETURN.
 * @param[in,out] rec   the entity's recorder
 * @param[in]     event what happened
 * @param[in]     room  the room's index
 * @param[in]     arg   as listed in enum RecordEvent
 */
void record_event(struct Recorder* rec, enum RecordEvent event, int room, int arg);

/**
 * Ends the entity's turn: waits one tick (--tick) and records that a turn passed.
 * Call it once at the end of every turn that doesn't exit, with no room or case
 * board locked.
 * @param[in,out] rec the entity's recorder
 */
void record_end_turn(struct Recorder* rec);

#endif
