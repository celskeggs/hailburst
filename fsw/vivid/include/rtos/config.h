#ifndef FSW_VIVID_RTOS_CONFIG_H
#define FSW_VIVID_RTOS_CONFIG_H

// #define TASK_DEBUG

/* set to the number of replicas for the scrubber to have, or 0 for no replicas */
#define VIVID_SCRUBBER_COPIES                           2

/* set to 1 if restarting clips should wait for the scrubbers before resuming */
#define VIVID_RECOVERY_WAIT_FOR_SCRUBBER                1

/* set to 1 if each task should be linked separately; 0 otherwise */
#define VIVID_REPLICATE_TASK_CODE                       1

/* set to 1 if exceptions should be recovered via clip restarts instead of system resets */
#define VIVID_RECOVER_FROM_EXCEPTIONS                   1

/* set to 1 if assertions and explicit restarts should actually restart the clip, as opposed to reset the system */
#define VIVID_RECOVER_FROM_ASSERTIONS                   1

/* set to 1 to enable assertions; otherwise, assertions will not be checked */
#define VIVID_CHECK_ASSERTIONS                          1

/* set to 0 for no preemption, 1 for maximum partition durations, and 2 (default) for maximum and minimum */
#define VIVID_PARTITION_SCHEDULE_ENFORCEMENT            2

/* set to 1 if the watchdog monitor clip should monitor the status of other components */
#define VIVID_WATCHDOG_MONITOR_ASPECTS                  1

/* set to 1 to enable the prepare/commit structure for the VIRTIO driver */
#define VIVID_PREPARE_COMMIT_VIRTIO_DRIVER              1

#endif /* FSW_VIVID_RTOS_CONFIG_H */
