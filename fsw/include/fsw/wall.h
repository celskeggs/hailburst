#ifndef FSW_WALL_H
#define FSW_WALL_H

/*
 * This file contains an implementation of a "pigeon-hole message wall" data structure.
 * This is a crash-safe IPC mechanism, where a set of many CLIENTS each have a "pigeon hole" that they can write a
 * request into. The SERVER can then consume these requests in fair FIFO order.
 *
 * Currently replies are not supported (because they would require an additional 'ACK' phase that has not yet been
 * demonstrated to be necessary), but this may be added later.
 */

#include <stdint.h>
#include <hal/thread.h>

enum {
    HOLE_NOT_FILLED = (uint64_t) -1,
};

typedef struct wall_st wall_t;
typedef struct hole_st hole_t;

typedef struct hole_st {
    wall_t *backing_wall;

    uint64_t message_index;  // set to HOLE_NOT_FILLED if not in linked list
    hole_t *next_filled_hole;

    void (*notify_client)(void *);
    void *client_param;

    size_t max_size;
    size_t actual_size;

    void *data;
} hole_t;

typedef struct wall_st {
    critical_t critical_section;

    void (*notify_server)(void *);
    void *server_param;

    uint64_t first_message_index;
    uint64_t next_message_index;
    hole_t *first_filled_hole;
    hole_t *last_filled_hole;
} wall_t;

// wall functions may ONLY be called by the single server task

// initializes a wall.
// notify_server should be a fast and non-blocking procedure that lets the server know to check the wall again.
void wall_init(wall_t *wall, void (*notify_server)(void *), void *param);
// destroys a wall
// (all holes should be destroyed first for safety)
void wall_destroy(wall_t *wall);
// queries the next available message, but does not remove it from the wall. returns NULL if no message is available.
// the caller may read the request, and then call wall_reply.
const void *wall_query(wall_t *wall, size_t *size_out);
// returns a message in a hole back to its sender. 'message' must be the most recent result from wall_query.
void wall_reply(wall_t *wall, const void *message);

// hole functions may ONLY be called by the associated client task

// initializes a hole.
// hole_size specifies the maximum size of a message to be allowed.
// notify_client should be a fast and non-blocking procedure that lets the server know to check the hole again.
void hole_init(hole_t *hole, size_t hole_size, wall_t *wall, void (*notify_client)(void *), void *param);
// if the hole is not filled, return a pointer to a place where a message can be written, otherwise NULL.
// each hole only has room for a single message, so if this is called multiple times without a send and returns
// non-NULL results, each result will be the same.
void *hole_prepare(hole_t *hole);
// if the hole is filled, return a pointer to the sent message (which may be reviewed), otherwise NULL.
const void *hole_peek(hole_t *hole);
// once a message is written into the buffer returned by hole_prepare, this function may be called to mark it filled.
void hole_send(hole_t *hole, size_t message_size);

static inline size_t hole_max_msg_size(hole_t *hole) {
    assert(hole != NULL);
    return hole->max_size;
}

#endif /* FSW_WALL_H */
