#include <string.h>

#include <hal/atomic.h>
#include <fsw/wall.h>

// initializes a wall.
// notify_server should be a fast and non-blocking procedure that lets the server know to check the wall again.
void wall_init(wall_t *wall, void (*notify_server)(void *), void *param) {
    assert(wall != NULL && notify_server != NULL);

    critical_init(&wall->critical_section);

    wall->notify_server = notify_server;
    wall->server_param = param;

    wall->first_message_index = 0;
    wall->next_message_index = 0;
    wall->first_filled_hole = NULL;
    wall->last_filled_hole = NULL;
}

void wall_destroy(wall_t *wall) {
    assert(wall != NULL);

    critical_destroy(&wall->critical_section);
}

// queries the next available message, but does not remove it from the wall. returns NULL if no message is available.
// the caller may read the request, and then call wall_reply.
const void *wall_query(wall_t *wall, size_t *size_out) {
    assert(wall != NULL);

    hole_t *first = atomic_load(wall->first_filled_hole);
    if (first == NULL) {
        return NULL;
    }
    assert(first->backing_wall == wall && first->message_index == wall->first_message_index && first->data != NULL
            && wall->first_message_index != HOLE_NOT_FILLED && first->actual_size <= first->max_size);
    *size_out = first->actual_size;

    return first->data;
}

// returns a message in a hole back to its sender. 'message' must be the most recent result from wall_query.
void wall_reply(wall_t *wall, const void *message) {
    assert(wall != NULL && message != NULL);

    // relaxed is sufficient because we already had a strong load in wall_query.
    hole_t *first = atomic_load_relaxed(wall->first_filled_hole);
    assert(first != NULL && first->backing_wall == wall && first->message_index == wall->first_message_index
            && wall->first_message_index != HOLE_NOT_FILLED && first->data == message);
    // we finish those checks before we enter the critical section to reduce the probability that we crash during the
    // critical section.

    critical_enter(&wall->critical_section);
    wall->first_message_index += 1;

    wall->first_filled_hole = first->next_filled_hole;
    if (wall->first_filled_hole == NULL) {
        assert(wall->last_filled_hole == first);
        wall->last_filled_hole = NULL;
        assert(wall->first_message_index == wall->next_message_index);
    }

    first->next_filled_hole = NULL;
    atomic_store(first->message_index, HOLE_NOT_FILLED);

    critical_exit(&wall->critical_section);

    first->notify_client(first->client_param);
}

// initializes a hole.
// hole_size specifies the maximum size of a message to be allowed.
// notify_client should be a fast and non-blocking procedure that lets the server know to check the hole again.
void hole_init(hole_t *hole, size_t hole_size, wall_t *wall, void (*notify_client)(void *), void *param) {
    assert(hole != NULL && wall != NULL && notify_client != NULL);
    assert(hole_size > 0);

    hole->backing_wall = wall;

    hole->message_index = HOLE_NOT_FILLED;
    hole->next_filled_hole = NULL;

    hole->notify_client = notify_client;
    hole->client_param = param;

    hole->max_size = hole_size;
    hole->actual_size = 0;

    hole->data = malloc(hole_size);
    assert(hole->data != NULL);
    memset(hole->data, 0, hole_size);
}

// if the hole is not filled, return a pointer to a place where a message can be written, otherwise NULL.
// each hole only has room for a single message, so if this is called multiple times without a send and returns
// non-NULL results, each result will be the same.
void *hole_prepare(hole_t *hole) {
    assert(hole != NULL);

    if (atomic_load(hole->message_index) != HOLE_NOT_FILLED) {
        return NULL;
    } else {
        assert(hole->next_filled_hole == NULL && hole->data != NULL);
        return hole->data;
    }
}

// if the hole is filled, return a pointer to the sent message (which may be reviewed), otherwise NULL.
const void *hole_peek(hole_t *hole) {
    assert(hole != NULL);

    if (atomic_load(hole->message_index) != HOLE_NOT_FILLED) {
        assert(hole->data != NULL);
        return hole->data;
    } else {
        assert(hole->next_filled_hole == NULL);
        return NULL;
    }
}

// once a message is written into the buffer returned by hole_prepare, this function may be called to mark it filled.
void hole_send(hole_t *hole, size_t message_size) {
    wall_t *wall = hole->backing_wall;
    assert(hole != NULL && wall != NULL && message_size <= hole->max_size);

    // relaxed is sufficient because we already had a strong load in hole_prepare.
    assert(atomic_load_relaxed(hole->message_index) == HOLE_NOT_FILLED);

    critical_enter(&wall->critical_section);

    hole->message_index = wall->next_message_index;
    hole->actual_size = message_size;
    hole->next_filled_hole = NULL;
    wall->next_message_index += 1;

    hole_t *last = wall->last_filled_hole;
    wall->last_filled_hole = hole;

    if (last == NULL) {
        assert(wall->first_filled_hole == NULL);
        assert(wall->first_message_index == hole->message_index);
        atomic_store(wall->first_filled_hole, hole);
    } else {
        last->next_filled_hole = hole;
    }

    critical_exit(&wall->critical_section);

    wall->notify_server(wall->server_param);
}
