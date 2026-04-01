#include <unity.h>

void test_simple_assertion() {
    TEST_ASSERT_EQUAL(1, 1);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_simple_assertion);
    return UNITY_END();
}
