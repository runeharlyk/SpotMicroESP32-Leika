// The inference code export_policy.py emits must reproduce the numpy forward pass it was checked
// against. In native builds <policy/leika_policy.h> resolves to esp32/test/fixtures: a small synthetic
// network in the same header format (written by simulation/export_golden.py) whose golden input
// drives both the observation and the action clip.

#include <unity.h>

#include <policy/leika_policy.h>

void setUp() {}
void tearDown() {}

void test_infer_reproduces_the_numpy_forward_pass() {
    const float err = leika_policy::selfCheck();
    TEST_ASSERT_TRUE_MESSAGE(err < 1e-5f, "infer() disagrees with the numpy reference");
}

void test_actions_stay_inside_the_trained_range() {
    float obs[leika_policy::OBS_DIM], act[leika_policy::ACT_DIM];
    for (int i = 0; i < leika_policy::OBS_DIM; ++i) obs[i] = 1e6f * (i % 2 ? 1.f : -1.f);
    leika_policy::infer(obs, act);
    for (float a : act) TEST_ASSERT_TRUE(a >= -1.f && a <= 1.f);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_infer_reproduces_the_numpy_forward_pass);
    RUN_TEST(test_actions_stay_inside_the_trained_range);
    return UNITY_END();
}
