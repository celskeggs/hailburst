#ifndef BARE_ARM_LINUX_POSIX_TYPES_H
#define BARE_ARM_LINUX_POSIX_TYPES_H

#include <bitsize/stdint.h>

typedef uintptr_t __kernel_size_t;

// stub types because these aren't usable on bare metal
typedef void __kernel_clock_t;
typedef void __kernel_caddr_t;
typedef void __kernel_daddr_t;
typedef struct {
    int fds_bits[0];
} __kernel_fd_set;
typedef void __kernel_fsid_t;
typedef int __kernel_gid32_t;
typedef void __kernel_ino_t;
typedef void __kernel_key_t;
typedef int __kernel_loff_t;
typedef int __kernel_mode_t;
typedef int __kernel_pid_t;
typedef void __kernel_suseconds_t;
typedef void __kernel_time_t;
typedef int __kernel_uid32_t;

#endif /* BARE_ARM_LINUX_POSIX_TYPES_H */
