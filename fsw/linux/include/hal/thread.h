#ifndef FSW_LINUX_HAL_THREAD_H
#define FSW_LINUX_HAL_THREAD_H

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>

enum {
    NS_PER_SEC = 1000 * 1000 * 1000,
};

#define THREAD_CHECK(x) (thread_check((x), #x))
#define THREAD_CHECK_OK(x, fm) (thread_check_ok((x), #x, (fm)))

typedef pthread_t       thread_t;
typedef pthread_mutex_t mutex_t;
// although there are semaphores available under POSIX, they are counting semaphores, and not binary semaphores.
typedef struct {
    mutex_t        mut;
    pthread_cond_t cond;
    bool           is_available;
} semaphore_t;

// queue and stream implementations based on the "good option" from here:
// https://www.snellman.net/blog/archive/2016-12-13-ring-buffers/

typedef struct {
    mutex_t mutex;
    pthread_cond_t cond;

    uint8_t *memory;
    size_t   item_size;
    size_t   capacity;
    // TODO: make sure I test integer overflow on these fields... SHOULD be fine, but needs to be tested
    size_t   read_scroll;
    size_t   write_scroll;
} queue_t;

typedef struct {
    mutex_t mutex;

    // semaphores to notify when data is ready to be read or written
    semaphore_t unblock_write;
    semaphore_t unblock_read;
    // advisory flags to catch simultaneous blocking writes or simultaneous blocking reads...
    // (also used for optimization purposes)
    bool blocked_write;
    bool blocked_read;

    uint8_t *memory;
    size_t   capacity;
    // TODO: make sure I test integer overflow or read_idx and write_idx... SHOULD be fine, but needs to be tested
    size_t   read_idx;
    size_t   write_idx;
} stream_t;

typedef mutex_t critical_t; // no such thing as a FreeRTOS critical section here, so fall back to mutexes

static inline void thread_check(int fail, const char *note) {
    if (fail != 0) {
        fprintf(stderr, "thread error: %d in %s\n", fail, note);
        abort();
    }
}

static inline bool thread_check_ok(int fail, const char *note, int false_marker) {
    if (fail == 0) {
        return true;
    } else if (fail == false_marker) {
        return false;
    } else {
        fprintf(stderr, "thread error: %d in %s\n", fail, note);
        abort();
    }
}

static inline void thread_cancel_impl(thread_t thread, const char *nt) {
    int err = pthread_cancel(thread);
    if (err != 0 && err != ESRCH) {
        fprintf(stderr, "thread error: %d in thread_cancel(%s)\n", err, nt);
        abort();
    }
}

#define mutex_init(x)     THREAD_CHECK(pthread_mutex_init((x), NULL))
#define mutex_destroy(x)  THREAD_CHECK(pthread_mutex_destroy(x))
#define mutex_lock(x)     THREAD_CHECK(pthread_mutex_lock(x))
#define mutex_lock_try(x) THREAD_CHECK_OK(pthread_mutex_trylock(x), EBUSY)
#define mutex_unlock(x)   THREAD_CHECK(pthread_mutex_unlock(x))

#define critical_init(c)    mutex_init(c)
#define critical_destroy(c) mutex_destroy(c)
#define critical_enter(c)   mutex_lock(c)
#define critical_exit(c)    mutex_unlock(c)

// name, priority, and restartable go unused on POSIX; these are only used on FreeRTOS
#define thread_create(x, name, priority, entrypoint, param, restartable) THREAD_CHECK(pthread_create((x), NULL, (entrypoint), (param)))
#define thread_join(x)                                      THREAD_CHECK(pthread_join((x), NULL))
#define thread_time_now(x)                                  THREAD_CHECK(clock_gettime(CLOCK_REALTIME, (x)))
#define thread_join_timed(x, t)                             THREAD_CHECK_OK(pthread_timedjoin_np((x), NULL, (t)), ETIMEDOUT)

// semaphores are created empty, such that an initial take will block
void semaphore_init(semaphore_t *sema);
void semaphore_destroy(semaphore_t *sema);

void semaphore_take(semaphore_t *sema);
// returns true if taken, false if not available
bool semaphore_take_try(semaphore_t *sema);
// returns true if taken, false if timed out
bool semaphore_take_timed(semaphore_t *sema, uint64_t nanoseconds);
// returns true if taken, false if timed out
bool semaphore_take_timed_abs(semaphore_t *sema, uint64_t deadline_ns);
bool semaphore_give(semaphore_t *sema);

extern void queue_init(queue_t *queue, size_t entry_size, size_t num_entries);
extern void queue_destroy(queue_t *queue);
extern bool queue_is_empty(queue_t *queue);
extern void queue_send(queue_t *queue, const void *new_item);
// returns true if sent, false if not
extern bool queue_send_try(queue_t *queue, void *new_item);
extern void queue_recv(queue_t *queue, void *new_item);
// returns true if received, false if not
extern bool queue_recv_try(queue_t *queue, void *new_item);
// returns true if received, false if timed out
extern bool queue_recv_timed_abs(queue_t *queue, void *new_item, uint64_t deadline_ns);

extern void stream_init(stream_t *stream, size_t capacity);
extern void stream_destroy(stream_t *stream);
// may only be used by a single thread at a time
extern void stream_write(stream_t *stream, uint8_t *data, size_t length);
// may only be used by a single thread at a time
extern size_t stream_read(stream_t *stream, uint8_t *data, size_t max_len);

#endif /* FSW_LINUX_HAL_THREAD_H */
