#ifndef COMMON_H
#define COMMON_H

/*
    Status codes shared by every assignment in the course.

    Functions that can fail return one of these. C_OK is zero and every error
    is negative. A function returns a status and nothing else; any quantity it
    produces is passed back through an output parameter.

    Codes are only ever added to this list. An existing code never changes its
    value or its meaning.
*/
enum {
    C_OK = 0,

    /* The calling code made a mistake. */
    C_ERR_BAD_ARG = -1,      /* A parameter is unusable, such as a NULL pointer. */
    C_ERR_OUT_OF_RANGE = -2, /* A numeric parameter is outside the permitted range. */

    /* The request was reasonable, but the data cannot satisfy it. */
    C_ERR_NOT_FOUND = -3,   /* Nothing matches what was asked for. */
    C_ERR_AT_CAPACITY = -4, /* The destination has no room for what was asked. */
    C_ERR_EMPTY = -5,       /* The collection has no items to remove or return. */
    C_ERR_CONFLICT = -6,    /* The item already exists, or is already in that state. */

    /* Something outside the program went wrong. */
    C_ERR_BAD_INPUT = -7, /* File contents or typed input do not match the expected format,
                             including a file that ends too soon. */
    C_ERR_IO = -8,        /* A file could not be opened, or a read or write operation failed. */
    C_ERR_ALLOC = -9,     /* A request for memory was refused. */

    /* Placeholder in code that has not been written yet. */
    C_ERR_NOT_IMPLEMENTED = -99
};

/*
    Returns a short human-readable description of a status code.

    in  code:  A status code from the enum above.
    Returns:
        - A pointer to a string literal. Never NULL. Do not free it.

    Functions are normally declared in a header and defined in a .c file.
    This one is a special case: it is defined here, as `static inline`, so
    that this header is the only file you need for the status codes. Do not
    define your own functions in header files.
*/
static inline const char *c_strerror(int code)
{
    switch (code) {
    case C_OK:                  return "success";
    case C_ERR_BAD_ARG:         return "unusable argument";
    case C_ERR_OUT_OF_RANGE:    return "value out of range";
    case C_ERR_NOT_FOUND:       return "not found";
    case C_ERR_AT_CAPACITY:     return "no room remaining";
    case C_ERR_EMPTY:           return "nothing to return";
    case C_ERR_CONFLICT:        return "already exists";
    case C_ERR_BAD_INPUT:       return "input does not match the expected format";
    case C_ERR_IO:              return "file could not be opened, read, or written";
    case C_ERR_ALLOC:           return "out of memory";
    case C_ERR_NOT_IMPLEMENTED: return "not implemented yet";
    default:                    return "unknown status code";
    }
}

#endif
