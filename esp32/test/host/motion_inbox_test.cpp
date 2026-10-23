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

// The control task reads its clock before it takes the mail, so an input posted in between carries a later time.
static void anInputPostedAfterTheTakeReadItsClockIsNoSilence() {
    MotionInbox inbox;
    inbox.postInput(sticks(0, 1.0f), 1001, 1001000);
    const MotionInbox::Mail mail = inbox.take(1000);
    CHECK(mail.input && !mail.linkLost);
    CHECK(inbox.take(1001 + MotionInbox::LINK_TIMEOUT_MS + 1).linkLost);
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

static void theNewestVariantIsTakenOnce() {
    MotionInbox inbox;
    inbox.postVariant(socket_message_KinematicsVariant_SPOTMICRO_ESP32);
    inbox.postVariant(socket_message_KinematicsVariant_SPOTMICRO_YERTLE);
    MotionInbox::Mail first = inbox.take(10);
    CHECK(first.variant == socket_message_KinematicsVariant_SPOTMICRO_YERTLE);
    CHECK(!inbox.take(20).variant);
}

static void aClipStopsWhenTheClientThatStartedItLeaves() {
    MotionInbox inbox;
    inbox.postPlay({nullptr, {}, 0}, 3);
    CHECK(inbox.take(10).play);
    inbox.dropClient(5);
    CHECK(!inbox.take(20).stopAnimation);
    inbox.dropClient(3);
    CHECK(inbox.take(30).stopAnimation);
    inbox.dropClient(3);
    CHECK(!inbox.take(40).stopAnimation);
}

// A pose is the client's as a clip is: its leaving eases the legs home, and a pose not yet taken goes with it.
static void aPoseIsDroppedAndStoppedWithTheClientThatSentIt() {
    MotionInbox inbox;
    anim::Pose pose;
    pose.body[anim::Z] = -10;
    inbox.postPose(pose, 3);
    const MotionInbox::Mail mail = inbox.take(10);
    CHECK(mail.pose && mail.pose->body[anim::Z] == -10);
    CHECK(!inbox.take(20).pose);
    inbox.postPose(pose, 3);
    inbox.dropClient(4);
    CHECK(inbox.take(30).pose);
    inbox.postPose(pose, 3);
    inbox.dropClient(3);
    const MotionInbox::Mail dropped = inbox.take(40);
    CHECK(!dropped.pose && dropped.stopAnimation);
}

static void aClipNotYetTakenIsDroppedWithItsClient() {
    MotionInbox inbox;
    inbox.postPlay({nullptr, {}, 0}, 3);
    inbox.dropClient(3);
    MotionInbox::Mail mail = inbox.take(10);
    CHECK(!mail.play && mail.stopAnimation);
}

static void aClipBelongsToTheClientThatStartedTheLatest() {
    MotionInbox inbox;
    inbox.postPlay({nullptr, {}, 0}, 3);
    inbox.postPlay({nullptr, {}, 0}, 4);
    inbox.take(10);
    inbox.dropClient(3);
    CHECK(!inbox.take(20).stopAnimation);
    inbox.dropClient(4);
    CHECK(inbox.take(30).stopAnimation);
}

static void aStoppedOrUnownedClipIgnoresLeavingClients() {
    MotionInbox inbox;
    inbox.postPlay({nullptr, {}, 0}, 3);
    inbox.postStopAnimation();
    inbox.take(10);
    inbox.dropClient(3);
    CHECK(!inbox.take(20).stopAnimation);
    inbox.postPlay({nullptr, {}, 0});
    inbox.take(30);
    inbox.dropClient(0);
    CHECK(!inbox.take(40).stopAnimation);
}

static void aGoalIsTakenOnceUnderItsNumber() {
    MotionInbox inbox;
    const uint32_t first = inbox.postGoal({0.2f, 0, 0, 0.5f});
    const uint32_t second = inbox.postGoal({0, 0.1f, 0, 0.5f});
    CHECK(first == 1 && second == 2);
    MotionInbox::Mail mail = inbox.take(10);
    CHECK(mail.goal && mail.goal->sideways == 0.1f && mail.goalId == second && !mail.cancelGoal);
    mail = inbox.take(20);
    CHECK(!mail.goal && mail.goalId == 0);
}

// A cancel drops a goal not yet taken; a goal posted after a cancel stands.
static void theLaterOfAGoalAndACancelWins() {
    MotionInbox inbox;
    inbox.postGoal({0.2f, 0, 0, 0.5f});
    inbox.postGoalCancel();
    MotionInbox::Mail mail = inbox.take(10);
    CHECK(!mail.goal && mail.cancelGoal);
    CHECK(!inbox.take(20).cancelGoal);
    inbox.postGoalCancel();
    inbox.postGoal({0.2f, 0, 0, 0.5f});
    mail = inbox.take(30);
    CHECK(mail.goal && !mail.cancelGoal);
}

static void aGoalIsCancelledWhenTheClientThatStartedTheLatestLeaves() {
    MotionInbox inbox;
    inbox.postGoal({0.2f, 0, 0, 0.5f}, 3);
    inbox.postGoal({0.2f, 0, 0, 0.5f}, 4);
    inbox.take(10);
    inbox.dropClient(3);
    CHECK(!inbox.take(20).cancelGoal);
    inbox.dropClient(4);
    CHECK(inbox.take(30).cancelGoal);
    inbox.dropClient(4);
    CHECK(!inbox.take(40).cancelGoal);
    inbox.postGoal({0.2f, 0, 0, 0.5f}); // from a link that never leaves
    inbox.take(50);
    inbox.dropClient(0);
    CHECK(!inbox.take(60).cancelGoal);
}

int main() {
    theNewestVariantIsTakenOnce();
    inputIsTakenOnce();
    theNewestInputWins();
    inputIsBoundedAndNonFiniteBecomesNeutral();
    aSilentControllerStopsTheRobotOnce();
    anInputPostedAfterTheTakeReadItsClockIsNoSilence();
    aControllerAtRestNeedsNoKeepAlive();
    newInputRearmsTheStop();
    modeAndGaitAreTakenOnce();
    anInputKeepsWhenItArrived();
    aClipStopsWhenTheClientThatStartedItLeaves();
    aClipNotYetTakenIsDroppedWithItsClient();
    aPoseIsDroppedAndStoppedWithTheClientThatSentIt();
    aClipBelongsToTheClientThatStartedTheLatest();
    aStoppedOrUnownedClipIgnoresLeavingClients();
    aGoalIsTakenOnceUnderItsNumber();
    theLaterOfAGoalAndACancelWins();
    aGoalIsCancelledWhenTheClientThatStartedTheLatestLeaves();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
