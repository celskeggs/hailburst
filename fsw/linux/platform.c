#include <stdio.h>

#include <hal/platform.h>

void platform_init(void) {
	freopen("/dev/console", "w", stdout);
	freopen("/dev/console", "w", stderr);
}
