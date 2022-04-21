#ifndef FSW_SYNCH_NOTEPAD_H
#define FSW_SYNCH_NOTEPAD_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <hal/debug.h>

/*
 * This file contains an implementation of a "voting state notepad." A notepad is a storage location for mutable state
 * for a replicated component, where instead of feeding forward mutable state separately within each replica (where it
 * could diverge from other replicas), the notepad votes on the state each scheduling cycle: this way, the replicas
 * should naturally re-synchronize after the scrubber finishes repairing any errors in code or read-only data.
 *
 * Optionally, one or more replicas from other modules can be registered as observers, which allows them to view the
 * voted-on state, but not modify it.
 */

enum {
    NOTEPAD_OBSERVER_BASE_ID = 100,

    NOTEPAD_UNINITIALIZED = 0xFF,
};

typedef const struct {
    const char *label;

    uint8_t  num_replicas;
    uint8_t  replica_id;
    uint8_t *flip_states; // each flip state is 0 or 1
    uint8_t *mutable_state;
    size_t   state_size;
} notepad_ref_t;

macro_define(NOTEPAD_REGISTER, n_ident, n_replicas, n_observers, n_state_size) {
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
    static_repeat(n_observers, n_observer_id) {
        notepad_ref_t symbol_join(n_ident, observer, n_observer_id) = {
            .label = symbol_str(n_ident),
            .num_replicas = (n_replicas),
            .replica_id = NOTEPAD_OBSERVER_BASE_ID + n_observer_id,
            .flip_states = symbol_join(n_ident, flip_states),
            .mutable_state = symbol_join(n_ident, mutable_state),
            .state_size = (n_state_size),
        };
    }
}

macro_define(NOTEPAD_REPLICA_REF, n_ident, n_replica_id) {
    (&symbol_join(n_ident, replica, n_replica_id))
}

macro_define(NOTEPAD_OBSERVER_REF, n_ident, n_observer_id) {
    (&symbol_join(n_ident, observer, n_observer_id))
}

// returns a structure populated with the current state, into which the new state should be written.
// note: replicas are assumed to execute in-order, and observe the previous cycle's data.
// if no valid data can be voted on, will clear the current state. *valid_out will be set to false.
void *notepad_feedforward(notepad_ref_t *replica, bool *valid_out);

// outputs the most recent written data from the last execution of all three replicas.
// note: observers are assumed to execute AFTER all replicas execute.
// returns false in case of reinitialization.
bool notepad_observe(notepad_ref_t *observer, void *output);

#endif /* FSW_SYNCH_NOTEPAD_H */
