#ifndef FSW_SYNCH_CONFIG_H
#define FSW_SYNCH_CONFIG_H

// CONFIG_SYNCH_MODE_STRICT can be set to one of two values:
//   [0] Timing and data errors will be considered recoverable. (Use for operation in the presence of radiation.)
//   [1] Timing and data errors will be considered crash-worthy. (Use for testing in the absence of radiation.)
#define CONFIG_SYNCH_MODE_STRICT 0

// CONFIG_APPLICATION_REPLICAS can be set to the number of replicas for ordinary application components to use.
#define CONFIG_APPLICATION_REPLICAS 3

#endif /* FSW_SYNCH_CONFIG_H */
