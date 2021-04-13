#define SERIAL_BASE 0x16000000
#define SERIAL_FLAG_REGISTER 0x18
#define SERIAL_BUFFER_FULL (1 << 5)

void putc (char c)
{
    /* Wait until the serial buffer is empty */
    while (*(volatile unsigned long*)(SERIAL_BASE + SERIAL_FLAG_REGISTER)
                                       & (SERIAL_BUFFER_FULL));
    /* Put our character, c, into the serial buffer */
    *(volatile unsigned long*)SERIAL_BASE = c;

    /* Print a carriage return if this is a newline, as the cursor's x position will not reset to 0*/
    if (c == '\n')
    {
        putc('\r');
    }
}

void puts (const char * str)
{
    while (*str) putc (*str++);
}

void putn (unsigned int i)
{
    if (i == 0) {
        putc('0');
    } else {
        if (i >= 10) {
            putn(i / 10);
        }
        putc('0' + (i % 10));
    }
}

void putx(unsigned int i)
{
    for (int j = sizeof(i) * 8 - 4; j >= 0; j -= 4) {
        putc("0123456789ABCDEF"[(i >> j)&15]);
    }
}

volatile int my_valuable_variable = 100;
volatile unsigned int scan_buffer[64 * 1024];

void scrub_memory(void)
{
    for (unsigned int i = 0; i < sizeof(scan_buffer) / sizeof(*scan_buffer); i++) {
        if (scan_buffer[i] != 0) {
            puts("memory error: addr=0x");
            putx((unsigned int) &scan_buffer[i]);
            puts(", value=0x");
            putx(scan_buffer[i]);
            puts("\n");
            scan_buffer[i] = 0;
        }
    }
}

int main (void)
{
    int pass = 0;
    puts ("hello, world!\n");
    while (1) {
        /* if (my_valuable_variable != 100) {
            puts ("memory error detected: ");
            putn (my_valuable_variable);
            puts ("\n");
            my_valuable_variable = 100;
        } */
        puts("scrubbing memory (pass #");
        putn(pass++);
        puts(")\n");
        scrub_memory();
    }
    return 0;
}
