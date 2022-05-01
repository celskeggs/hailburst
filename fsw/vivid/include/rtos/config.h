#ifndef FSW_VIVID_RTOS_CONFIG_H
#define FSW_VIVID_RTOS_CONFIG_H

// #define TASK_DEBUG

/* set to the number of replicas for the scrubber to have, or 0 for no replicas */
#define VIVID_SCRUBBER_COPIES                           2

/* set to 1 if each task should be linked separately; 0 otherwise */
#define VIVID_REPLICATE_TASK_CODE                       1

/* set to 1 if exceptions should be recovered via clip restarts instead of system resets */
#define VIVID_RECOVER_FROM_EXCEPTIONS                   1

/* set to 1 if assertions and explicit restarts should actually restart the clip, as opposed to reset the system */
#define VIVID_RECOVER_FROM_ASSERTIONS                   1

#endif /* FSW_VIVID_RTOS_CONFIG_H */
