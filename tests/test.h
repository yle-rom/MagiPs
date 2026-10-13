#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>

/* Tiny test harness: CHECK records a failure with file:line and keeps going. */
extern int test_checks, test_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        test_checks++;                                                       \
        if (!(cond)) {                                                       \
            test_failures++;                                                 \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
        }                                                                    \
    } while (0)

#define CHECK_STR(a, b) CHECK(strcmp((a), (b)) == 0)

#define FIXTURE_PROC "tests/fixtures/proc"
#define FIXTURE_APPS "tests/fixtures/applications"
#define FIXTURE_UID  1000

void test_procfs(void);
void test_desktop(void);
void test_apps_services(void);
void test_ports(void);

#endif
