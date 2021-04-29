#ifndef APP_APP_H
#define APP_APP_H

typedef void (*TaskFunction)(void);

typedef struct {
    const char *name;
    TaskFunction func;
} TaskSpec;

void task_scrub_memory(void);

void init_iotest(void);

void task_iotest_transmitter(void);
void task_iotest_receiver(void);

#endif /* APP_APP_H */