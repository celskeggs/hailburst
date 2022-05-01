#ifndef FSW_SYNCH_NOTEPAD_H
#define FSW_SYNCH_NOTEPAD_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <hal/debug.h>
#include <synch/config.h>

/*
 * This file contains an implementation of a "voting state notepad." A notepad is a storage location for mutable state
 * for a replicated component, where instead of feeding forward mutable state separately within each replica (where it
 * could diverge from other replicas), the notepad votes on the state each scheduling cycle: this way, the replicas
 * should naturally re-synchronize after the scrubber finishes repairing any errors in code or read-only data.
 */

enum {
    NOTEPAD_UNINITIALIZED = 0xFF,
};

#if ( CONFIG_SYNCH_NOTEPADS_ENABLED == 1 )

typedef const struct {
    const char *label;

    uint8_t  num_replicas;
    uint8_t  replica_id;
    uint8_t *flip_states; // each flip state is 0 or 1
    uint8_t *mutable_state;
    size_t   state_size;
} notepad_ref_t;

macro_define(NOTEPAD_REGISTER, n_ident, n_replicas, n_state_size) {
    uint8_t symbol_join(n_ident, flip_states)[n_replicas] = { [0 ... n_replicas - 1] = NOTEPAD_UNINITIALIZED };
    uint8_t symbol_join(n_ident, mutable_state)[(n_replicas) * 2 * (n_state_size)];
    static_repeat(n_replicas, n_replica_id) {
        notepad_ref_t symbol_join(n_ident, replica, n_replica_id) = {
            .label = symbol_str(n_ident),
            .num_replicas = (n_replicas),
            .replica_id = n_replica_id,
            .flip_states = symbol_join(n_ident, flip_states),
            .mutable_state = symbol_join(n_ident, mutable_state),
            .state_size = (n_state_size),
        };
    }
}

#else /* ( CONFIG_SYNCH_NOTEPADS_ENABLED == 0 ) */

typedef const struct {
    uint8_t *local_buffer;
} notepad_ref_t;

macro_define(NOTEPAD_REGISTER, n_ident, n_replicas, n_state_size) {
    static_repeat(n_replicas, n_replica_id) {
        uint8_t symbol_join(n_ident, local_buffer, n_replica_id)[n_state_size];
        notepad_ref_t symbol_join(n_ident, replica, n_replica_id) = {
            .local_buffer = symbol_join(n_ident, local_buffer, n_replica_id),
        };
    }
}

#endif

macro_define(NOTEPAD_REPLICA_REF, n_ident, n_replica_id) {
    (&symbol_join(n_ident, replica, n_replica_id))
}

// returns a structure populated with the current state, into which the new state should be written.
// note: replicas are assumed to execute in-order, and observe the previous cycle's data.
// if no valid data can be voted on, will clear the current state. *valid_out will be set to false.
void *notepad_feedforward(notepad_ref_t *replica, bool *valid_out);

#endif /* FSW_SYNCH_NOTEPAD_H */
