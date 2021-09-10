#include <hal/platform.h>
#include <hal/thread.h>

static bool initialized = false;
static pthread_key_t wakeup_semaphore_key;

static void wakeup_semaphore_destroy(void *ptr) {
    semaphore_t *sema = (semaphore_t *) ptr;
    if (sema != NULL) {
        semaphore_destroy(sema);
        free(sema);
    }
}

wakeup_t wakeup_open(void) {
    assert(initialized == true);
    semaphore_t *sema = (semaphore_t *) pthread_getspecific(wakeup_semaphore_key);
    if (sema == NULL) {
        sema = malloc(sizeof(semaphore_t));
        semaphore_init(sema);
        THREAD_CHECK(pthread_setspecific(wakeup_semaphore_key, sema));
        assert(sema == (semaphore_t *) pthread_getspecific(wakeup_semaphore_key));
    }
    semaphore_reset_linuxonly(sema);
    return sema;
}

void wakeup_system_init(void) {
    assert(!initialized);
    THREAD_CHECK(pthread_key_create(&wakeup_semaphore_key, wakeup_semaphore_destroy));
    initialized = true;
}

void platform_init(void) {
	freopen("/dev/console", "w", stdout);
	freopen("/dev/console", "w", stderr);

    wakeup_system_init();
}
