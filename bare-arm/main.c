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

unsigned int get_trace_count(void)
{
    return *(volatile unsigned int *) 0x800000;
}

int main (void)
{
    unsigned int lcount = 0xFFFFFFFF, ncount;
    puts ("hello, world!\n");
    while (1) {
        ncount = get_trace_count();
        if (ncount != lcount) {
            puts ("count: ");
            putn (ncount);
            puts ("\n");
            lcount = ncount;
        }
    }
    return 0;
}
