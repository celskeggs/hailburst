#ifndef BARE_ARM_LINUX_TIME_H
#define BARE_ARM_LINUX_TIME_H

struct timezone;
struct timeval;
struct itimerval;

struct timespec {
    long tv_sec;
    long tv_nsec;
};

#endif /* BARE_ARM_LINUX_TIME_H */
