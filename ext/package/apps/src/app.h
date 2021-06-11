#ifndef APP_APP_H
#define APP_APP_H

typedef void (*TaskFunction)(void);

typedef struct {
    const char *name;
    TaskFunction init;
    TaskFunction func;
} TaskSpec;

void init_iotest(void);
void task_iotest_transmitter(void);
void task_iotest_receiver(void);

void init_rmap_listener(void);
void task_rmap_listener(void);

void task_scrub_memory(void);

#endif /* APP_APP_H */