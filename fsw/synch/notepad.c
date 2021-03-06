#include <string.h>

#include <hal/clip.h>
#include <synch/notepad.h>
#include <synch/strict.h>

#if ( CONFIG_SYNCH_NOTEPADS_ENABLED == 1 )

static inline uint8_t *notepad_region_ref(notepad_ref_t *replica, uint8_t replica_id, uint8_t flip_state) {
    assert(replica != NULL);
    assert(flip_state == 0 || flip_state == 1);
    return &replica->mutable_state[(replica_id * 2 + flip_state) * replica->state_size];
}

static inline uint8_t notepad_vote_flip(notepad_ref_t *replica) {
    assert(replica != NULL);
    uint32_t count_secondary = 0;
    if (replica->flip_states[replica->replica_id] == NOTEPAD_UNINITIALIZED) {
        return NOTEPAD_UNINITIALIZED;
    }

    for (uint8_t i = 0; i < replica->num_replicas; i++) {
        uint8_t flip_state = replica->flip_states[i];
        if (flip_state & ~1) {
            debugf(WARNING, "Notepad %s[%u] detected invalid value for flip_states bit: 0x%02x.",
                   replica->label, replica->replica_id, flip_state);
        }
        flip_state &= 1;
        if (i < replica->replica_id) {
            // this replica has already executed before us, so assume its current flip state setting has already been
            // flipped for next cycle.
            flip_state ^= 1;
        }
        // count votes
        if (flip_state == 1) {
            count_secondary++;
        }
    }
    uint32_t majority = 1 + (replica->num_replicas / 2);
    return (count_secondary >= majority) ? 1 : 0;
}

static bool notepad_vote_best(notepad_ref_t *replica, uint8_t flip_read, void *output_region) {
    if (flip_read == NOTEPAD_UNINITIALIZED) {
        debugf(DEBUG, "Blank notepad %s[%u]; initializing by reset.", replica->label, replica->replica_id);
        memset(output_region, 0, replica->state_size);
        return false; /* invalid */
    }

    bool populated = false;
    uint8_t best_vote = 0;
    uint8_t majority = 1 + (replica->num_replicas / 2);
    for (uint8_t candidate_id = 0; candidate_id <= replica->num_replicas - majority; candidate_id++) {
        uint8_t *candidate_data = notepad_region_ref(replica, candidate_id, flip_read);
        uint8_t votes = 1;
        for (uint8_t compare_id = candidate_id + 1; compare_id < replica->num_replicas; compare_id++) {
            uint8_t *compare_data = notepad_region_ref(replica, compare_id, flip_read);
            if (memcmp(candidate_data, compare_data, replica->state_size) == 0) {
                votes++;
            }
        }
        if (votes > best_vote) {
            best_vote = votes;
        }
        if (votes >= majority) {
            memcpy(output_region, candidate_data, replica->state_size);
            populated = true;
            break;
        }
    }

    if (!populated) {
        miscomparef("No valid feedforward state found in notepad %s[%u], as best vote matched %u/%u; resetting.",
                    replica->label, replica->replica_id, best_vote, replica->num_replicas);
        memset(output_region, 0, replica->state_size);
        return false; /* invalid */
    } else if (best_vote < replica->num_replicas) {
        miscomparef("Feedforward state in notepad %s[%u] matched %u/%u; other data tossed.",
                    replica->label, replica->replica_id, best_vote, replica->num_replicas);
    }
    return true; /* valid */
}

// returns a structure populated with the current state, into which the new state should be written.
void *notepad_feedforward(notepad_ref_t *replica, bool *valid_out) {
    assert(replica != NULL);
    assert(replica->replica_id < replica->num_replicas); // ensure this is not an observer

    uint8_t flip_read = notepad_vote_flip(replica);
    uint8_t flip_write = !flip_read; // opposite of flip_read; map flip_read=NOTEPAD_UNINITIALIZED to flip_write=0
    assert(flip_write == 0 || flip_write == 1);

    void *output_region = notepad_region_ref(replica, replica->replica_id, flip_write);

    bool valid = notepad_vote_best(replica, flip_read, output_region);
    if (valid_out != NULL) {
        *valid_out = valid;
    }

    // designate the new region as containing new data
    replica->flip_states[replica->replica_id] = flip_write;

    // return the output region
    return output_region;
}

#else /* ( CONFIG_SYNCH_NOTEPADS_ENABLED == 0 ) */

void *notepad_feedforward(notepad_ref_t *replica, bool *valid_out) {
    assert(replica != NULL);
    if (valid_out != NULL) {
        *valid_out = !clip_is_restart();
    }
    return replica->local_buffer;
}

#endif
