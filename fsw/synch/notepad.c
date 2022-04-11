#include <string.h>

#include <synch/config.h>
#include <synch/notepad.h>

static inline uint8_t *notepad_region_ref(notepad_ref_t *replica, uint8_t replica_id, uint8_t flip_state) {
    assert(replica != NULL);
    assert(flip_state == 0 || flip_state == 1);
    return &replica->mutable_state[(replica_id * 2 + flip_state) * replica->state_size];
}

static inline uint8_t notepad_vote_flip(notepad_ref_t *replica, bool is_observer) {
    assert(replica != NULL);
    uint32_t count_secondary = 0;
    for (size_t i = 0; i < replica->num_replicas; i++) {
        uint8_t flip_state = replica->flip_states[i];
        assert(flip_state == 0 || flip_state == 1);
        if (!is_observer && i < replica->replica_id) {
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

static bool notepad_vote_best(notepad_ref_t *replica, bool flip_read, void *output_region) {
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

    uint8_t flip_read = notepad_vote_flip(replica, false);
    uint8_t flip_write = (flip_read ^ 1);

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

bool notepad_observe(notepad_ref_t *observer, void *output) {
    assert(observer != NULL);
    assert(observer->replica_id >= observer->num_replicas); // ensure this is not a regular replica

    uint8_t flip_read = notepad_vote_flip(observer, true);

    return notepad_vote_best(observer, flip_read, output);
}
