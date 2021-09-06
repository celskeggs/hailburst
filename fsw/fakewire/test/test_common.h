#ifndef APP_TEST_COMMON_H
#define APP_TEST_COMMON_H

void test_common_make_fifos(const char *infix);
void test_common_get_fifo(const char *infix, char *out, size_t len);
void test_common_get_fifo_p2c(const char *infix, char *out, size_t len);
void test_common_get_fifo_c2p(const char *infix, char *out, size_t len);

// test should return 0 on success, nonzero on error
int test_main(void);

#endif /* APP_TEST_COMMON_H */