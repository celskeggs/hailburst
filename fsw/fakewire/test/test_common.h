#ifndef APP_TEST_COMMON_H
#define APP_TEST_COMMON_H

void test_common_make_fifos(const char *prefix);

// test should return 0 on success, nonzero on error
int test_main(void);

#endif /* APP_TEST_COMMON_H */