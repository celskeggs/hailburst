#ifndef BARE_ARM_LINUX_TERMIOS_H
#define BARE_ARM_LINUX_TERMIOS_H

struct termios {
    int c_cflag;
};
typedef int speed_t;

#define CBAUD 0010017

#endif /* BARE_ARM_LINUX_TERMIOS_H */
