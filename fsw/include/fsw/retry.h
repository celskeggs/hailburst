#ifndef FSW_FAKEWIRE_RETRY_H
#define FSW_FAKEWIRE_RETRY_H

#include <stdbool.h>
#include <stdint.h>

#include <fsw/debug.h>

#define RETRY(n_retries, fmt, ...) \
    for ( \
        unsigned int _retries = n_retries; \
        _retries > 0; \
        (--_retries) ? \
            debugf(CRITICAL, "Retrying " fmt, ## __VA_ARGS__) : \
            debugf(CRITICAL, "After %u retries, erroring out during " fmt, n_retries, ## __VA_ARGS__) \
    )

#endif /* FSW_FAKEWIRE_RETRY_H */
