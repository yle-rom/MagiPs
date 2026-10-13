#include "test.h"

int test_checks, test_failures;

int main(void)
{
    test_procfs();
    test_desktop();
    test_apps_services();
    test_ports();
    test_config();
    test_db();
    test_devserver();
    test_json();

    printf("%d checks, %d failed\n", test_checks, test_failures);
    return test_failures ? 1 : 0;
}
