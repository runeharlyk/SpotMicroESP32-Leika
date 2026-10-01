// Host test of motion_inbox.h, built and run by test_host_programs.py.
#include <cmath>
#include <cstdio>
#include <motion_inbox.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static socket_message_ControllerData sticks(float lx, float ly, float height = 0.5f) {
    socket_message_ControllerData data = socket_message_ControllerData_init_zero;
    data.has_left = data.has_right = true;
    data.left.x = lx;
    data.left.y = ly;
    data.height = height;
    data.speed = 0.5f;
    return data;
}

static void inputIsTakenOnce() {
    MotionInbox inbox;
    inbox.postInput(sticks(0.2f, 0.4f), 0, 0);
    MotionInbox::Mail first = inbox.take(10);
    CHECK(first.input && first.input->lx == 0.2f && first.input->ly == 0.4f);
    CHECK(!inbox.take(20).input);
}

static void theNewestInputWins() {
    MotionInbox inbox;
    inbox.postInput(sticks(0.2f, 0), 0, 0);
    inbox.postInput(sticks(0.7f, 0), 5, 5000);
    CHECK(inbox.take(10).input->lx == 0.7f);
}

static void inputIsBoundedAndNonFiniteBecomesNeutral() {
    MotionInbox inbox;
    socket_message_ControllerData data = sticks(NAN, 5.0f, 2.0f);
    data.right.x = -3.0f;
    data.right.y = INFINITY;
    data.speed = -1.0f;
    data.s1 = NAN;
    inbox.postInput(data, 0, 0);
    CommandMsg command = *inbox.take(1).input;
    CHECK(command.lx == 0.0f);
    CHECK(command.ly == 1.0f);
    CHECK(command.rx == -1.0f);
    CHECK(command.ry == 0.0f);
    CHECK(command.h == 1.0f);
    CHECK(command.s == 0.0f);
    CHECK(command.s1 == 0.0f);
}

static void aSilentControllerStopsTheRobotOnce() {
    MotionInbox inbox;
    inbox.postInput(sticks(0, 1.0f), 1000, 1000000);
    inbox.take(1000);
    CHECK(!inbox.take(1000 + MotionInbox::LINK_TIMEOUT_MS).linkLost);
    CHECK(inbox.take(1001 + MotionInbox::LINK_TIMEOUT_MS).linkLost);
    CHECK(!inbox.take(3000).linkLost);
}

static void aControllerAtRestNeedsNoKeepAlive() {
    MotionInbox inbox;
    inbox.postInput(sticks(0, 0), 0, 0);
    inbox.take(0);
    CHECK(!inbox.take(10000).linkLost);
}

static void newInputRearmsTheStop() {
    MotionInbox inbox;
    inbox.postInput(sticks(0, 1.0f), 0, 0);
    inbox.take(0);
    CHECK(inbox.take(600).linkLost);
    inbox.postInput(sticks(0.5f, 0), 700, 700000);
    CHECK(inbox.take(710).input);
    CHECK(inbox.take(1300).linkLost);
}

static void modeAndGaitAreTakenOnce() {
    MotionInbox inbox;
    inbox.postMode(socket_message_ModesEnum_WALK);
    inbox.postGait(socket_message_WalkGaits_CRAWL);
    MotionInbox::Mail mail = inbox.take(0);
    CHECK(mail.mode && *mail.mode == socket_message_ModesEnum_WALK);
    CHECK(mail.gait && *mail.gait == socket_message_WalkGaits_CRAWL);
    MotionInbox::Mail next = inbox.take(1);
    CHECK(!next.mode && !next.gait);
}

static void anInputKeepsWhenItArrived() {
    MotionInbox inbox;
    socket_message_ControllerData data = socket_message_ControllerData_init_zero;
    inbox.postInput(data, 1000, 1000123);
    const MotionInbox::Mail mail = inbox.take(1010);
    CHECK(mail.input.has_value());
    CHECK(mail.inputAtUs == 1000123);
    CHECK(inbox.take(1020).inputAtUs == 0);
}

int main() {
    inputIsTakenOnce();
    theNewestInputWins();
    inputIsBoundedAndNonFiniteBecomesNeutral();
    aSilentControllerStopsTheRobotOnce();
    aControllerAtRestNeedsNoKeepAlive();
    newInputRearmsTheStop();
    modeAndGaitAreTakenOnce();
    anInputKeepsWhenItArrived();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
