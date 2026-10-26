// Host test of the body-frame sign convention against platform_shared/sign_convention.json, which the simulation's
// test_sign_convention.py and the app's sign-convention.spec.ts pass too, so no copy of the motion code can mirror an
// axis on its own; built and run by test_host_programs.py, which passes the fixture's path. The legs are checked on
// every variant's KinConfig, the attitudes on the kinematics, the walk and the stand from the controller message on,
// through the motion states, and the odometry and the goals through MotionService. docs/conventions.md derives the
// signs.
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <motion.h>
#include <variant.h>

static int failures = 0;

#define CHECK_SIGN(expected, actual, where)                                                                       \
    do {                                                                                                          \
        if ((expected) != (actual)) {                                                                             \
            std::printf("FAIL %s:%d: %s: sign %d, expected %d\n", __FILE__, __LINE__, std::string(where).c_str(), \
                        static_cast<int>(actual), static_cast<int>(expected));                                    \
            failures++;                                                                                           \
        }                                                                                                         \
    } while (0)

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

namespace {

// The fixture's JSON: objects, arrays, numbers and strings without escapes. A missing key or index reads as 0 and
// counts as a failure.
struct Json {
    enum Kind { NUMBER, STRING, ARRAY, OBJECT } kind = NUMBER;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;

    const Json &operator[](const std::string &key) const {
        for (const auto &[name, value] : fields)
            if (name == key) return value;
        return missing("key " + key);
    }
    const Json &operator[](size_t index) const {
        return index < items.size() ? items[index] : missing("index " + std::to_string(index));
    }
    float f() const { return static_cast<float>(number); }
    int i() const { return static_cast<int>(number); }
    size_t size() const { return items.size(); }

  private:
    static const Json &missing(const std::string &what) {
        static const Json none;
        std::printf("FAIL: the fixture has no %s\n", what.c_str());
        failures++;
        return none;
    }
};

struct Parser {
    const std::string &s;
    size_t at = 0;
    bool ok = true;

    void space() {
        while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at]))) at++;
    }
    bool accept(char c) {
        space();
        if (at < s.size() && s[at] == c) return ++at, true;
        return false;
    }
    void expect(char c) {
        if (!accept(c)) ok = false;
    }
    std::string string() {
        expect('"');
        const size_t end = s.find('"', at);
        if (!ok || end == std::string::npos) return ok = false, "";
        std::string out = s.substr(at, end - at);
        at = end + 1;
        return out;
    }
    Json value() {
        Json v;
        space();
        if (!ok || at >= s.size()) return ok = false, v;
        if (accept('{')) {
            v.kind = Json::OBJECT;
            if (accept('}')) return v;
            do {
                std::string key = string();
                expect(':');
                v.fields.emplace_back(std::move(key), value());
            } while (ok && accept(','));
            expect('}');
        } else if (accept('[')) {
            v.kind = Json::ARRAY;
            if (accept(']')) return v;
            do v.items.push_back(value());
            while (ok && accept(','));
            expect(']');
        } else if (s[at] == '"') {
            v.kind = Json::STRING;
            v.text = string();
        } else {
            char *end;
            v.number = std::strtod(s.c_str() + at, &end);
            if (end == s.c_str() + at) ok = false;
            at = end - s.c_str();
        }
        return v;
    }
};

Json readFixture(const char *path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    const std::string json = text.str();
    Parser parser {json};
    Json fixture = parser.value();
    if (!parser.ok || fixture.kind != Json::OBJECT) {
        std::printf("FAIL: %s is missing or not the fixture's JSON\n", path);
        failures++;
        return {};
    }
    return fixture;
}

int sign(float value, float zero) { return value > zero ? 1 : (value < -zero ? -1 : 0); }

struct Variant {
    const char *name;
    const KinConfig &config;
};
const Variant VARIANTS[] = {{"SPOTMICRO_ESP32", KIN_CONFIG_SPOTMICRO_ESP32},
                            {"SPOTMICRO_ESP32_MINI", KIN_CONFIG_SPOTMICRO_ESP32_MINI},
                            {"SPOTMICRO_YERTLE", KIN_CONFIG_SPOTMICRO_YERTLE}};

socket_message_ControllerData controller(float lx, float ly, float rx, float ry) {
    socket_message_ControllerData data = socket_message_ControllerData_init_zero;
    data.has_left = data.has_right = true;
    data.left.x = lx;
    data.left.y = ly;
    data.right.x = rx;
    data.right.y = ry;
    data.height = 0.5f;
    data.speed = 0.5f;
    data.s1 = 0.5f;
    return data;
}

// A point in the kinematics' frame (x forward, y up, z left) in the fixture's REP-103 one (forward, left, up).
struct Rep {
    float forward, left, up;
};
Rep rep(const float p[3]) { return {p[0], p[2], p[1]}; }

// The feet in the body frame, as Kinematics::calculate_inverse_kinematics moves them before it solves each leg:
// p_b = R^T (p_w - t), in REP-103. That the IK solves the same legs for a level body at the origin standing on these
// feet is checked, so they are the feet the IK places, not a reading of its formula.
std::vector<Rep> bodyFrameFeet(const KinConfig &config, const body_state_t &body) {
    Kinematics kinematics(config);
    float rot[3][3];
    kinematics.euler2R(body.omega * DEG2RAD_F, body.phi * DEG2RAD_F, body.psi * DEG2RAD_F, rot);
    const float t[3] = {body.xm, body.ym, body.zm};
    body_state_t level {};
    for (int leg = 0; leg < 4; leg++) {
        for (int row = 0; row < 3; row++) {
            level.feet[leg][row] = 0;
            for (int k = 0; k < 3; k++) level.feet[leg][row] += rot[k][row] * (body.feet[leg][k] - t[k]);
        }
        level.feet[leg][3] = 1;
    }
    float posed[12], levelled[12];
    kinematics.calculate_inverse_kinematics(body, posed);
    kinematics.calculate_inverse_kinematics(level, levelled);
    for (int joint = 0; joint < 12; joint++) CHECK(std::fabs(posed[joint] - levelled[joint]) < 1e-3f);
    std::vector<Rep> feet;
    for (int leg = 0; leg < 4; leg++) feet.push_back(rep(level.feet[leg]));
    return feet;
}

// The body over its feet: world up is the normal of the ground the feet span, the body's offset is the part along that
// ground of the way from the feet's centre to the body's origin, and its yaw is the turn that carries the feet of
// `rest` about their centre onto these, negated, as the feet stand still while the body turns.
struct Pose {
    Rep up;
    float offset[2];
    std::vector<Rep> feet;
};

Pose poseOverFeet(const Json &fixture, const std::vector<Rep> &feet) {
    const Json &legs = fixture["legs"];
    Rep across {}, along {}, centre {};
    for (int leg = 0; leg < 4; leg++) {
        const Rep &f = feet[leg];
        const float side = legs[leg]["side"][1].f() / 2, end = legs[leg]["side"][0].f() / 2;
        across = {across.forward + side * f.forward, across.left + side * f.left, across.up + side * f.up};
        along = {along.forward + end * f.forward, along.left + end * f.left, along.up + end * f.up};
        centre = {centre.forward + f.forward / 4, centre.left + f.left / 4, centre.up + f.up / 4};
    }
    Rep up {along.left * across.up - along.up * across.left, along.up * across.forward - along.forward * across.up,
            along.forward * across.left - along.left * across.forward};
    const float length = std::sqrt(up.forward * up.forward + up.left * up.left + up.up * up.up);
    up = {up.forward / length, up.left / length, up.up / length};
    const float height = centre.forward * up.forward + centre.left * up.left + centre.up * up.up;
    return {up, {-centre.forward + height * up.forward, -centre.left + height * up.left}, feet};
}

float yawFrom(const Pose &rest, const Pose &held) {
    float centres[2][2] = {};
    for (int leg = 0; leg < 4; leg++) {
        centres[0][0] += rest.feet[leg].forward / 4, centres[0][1] += rest.feet[leg].left / 4;
        centres[1][0] += held.feet[leg].forward / 4, centres[1][1] += held.feet[leg].left / 4;
    }
    float cross = 0, dot = 0;
    for (int leg = 0; leg < 4; leg++) {
        const float px = rest.feet[leg].forward - centres[0][0], py = rest.feet[leg].left - centres[0][1];
        const float qx = held.feet[leg].forward - centres[1][0], qy = held.feet[leg].left - centres[1][1];
        cross += px * qy - py * qx;
        dot += px * qx + py * qy;
    }
    return -std::atan2(cross, dot);
}

// Roll lowers the right side, so it tips world up toward body left; pitch lowers the nose, so it tips world up toward
// body backward.
void checkRotation(const Json &expected, const Pose &rest, const Pose &held, float zero, const std::string &where) {
    CHECK_SIGN(expected[0].i(), sign(held.up.left, zero), where + ", roll");
    CHECK_SIGN(expected[1].i(), sign(-held.up.forward, zero), where + ", pitch");
    CHECK_SIGN(expected[2].i(), sign(yawFrom(rest, held), zero), where + ", yaw");
}

// MotionService's way with the controller message and the states (esp32/src/motion.cpp), without its timer, so the
// body state is in reach: the message becomes the command, the state in force takes it and steps the body.
struct States {
    const KinConfig &config;
    StandState stand;
    WalkState walk;
    MotionState *state = nullptr;
    body_state_t body;
    float dt;

    States(const KinConfig &config, float dt) : config(config), dt(dt) {
        stand.configure(config);
        walk.configure(config);
        body.ym = config.default_body_height;
        body.updateFeet(config.default_feet_positions);
    }
    void enter(MotionState &next) {
        if (state) state->end();
        state = &next;
        state->resetSmoothing();
        state->begin();
    }
    void input(const socket_message_ControllerData &data) {
        CommandMsg command;
        command.fromProto(data);
        state->handleCommand(command);
    }
    void tick() {
        state->updateImuOffsets(ImuSample {});
        state->step(body, dt);
    }
    void hold(const socket_message_ControllerData &data, int ticks) {
        input(data);
        for (int i = 0; i < ticks; i++) tick();
    }
};

void legsSitOnTheirFixtureSides(const Json &fixture) {
    const Json &legs = fixture["legs"];
    CHECK(legs.size() == 4);
    for (const Variant &v : VARIANTS)
        for (int leg = 0; leg < 4; leg++) {
            const std::string where = std::string(v.name) + ", " + legs[leg]["name"].text;
            const Rep mount = rep(v.config.mountOffsets[leg]), foot = rep(v.config.default_feet_positions[leg]);
            CHECK_SIGN(legs[leg]["side"][0].i(), sign(mount.forward, 0), where + " mount, forward");
            CHECK_SIGN(legs[leg]["side"][1].i(), sign(mount.left, 0), where + " mount, left");
            CHECK_SIGN(legs[leg]["side"][0].i(), sign(foot.forward, 0), where + " foot, forward");
            CHECK_SIGN(legs[leg]["side"][1].i(), sign(foot.left, 0), where + " foot, left");
        }
}

void attitudesRotateTheBodyWithTheFixtureSigns(const Json &fixture) {
    const Json &attitude = fixture["attitude"];
    CHECK(attitude["poses"].size() == 3);
    for (const Variant &v : VARIANTS) {
        body_state_t body;
        body.ym = v.config.default_body_height;
        body.updateFeet(v.config.default_feet_positions);
        const Pose rest = poseOverFeet(fixture, bodyFrameFeet(v.config, body));
        for (const Json &row : attitude["poses"].items) {
            body.omega = row["degrees"]["omega"].f();
            body.phi = row["degrees"]["phi"].f();
            body.psi = row["degrees"]["psi"].f();
            const Pose posed = poseOverFeet(fixture, bodyFrameFeet(v.config, body));
            checkRotation(row["rotation_sign"], rest, posed, attitude["zero_rad"].f(),
                          std::string(v.name) + ", " + row["name"].text);
        }
    }
}

void standSticksShiftAndRotateTheBodyWithTheFixtureSigns(const Json &fixture) {
    const Json &stand = fixture["stand"];
    CHECK(stand["sticks"].size() == 4);
    const int ticks = stand["settle_ticks"].i();
    for (const Variant &v : VARIANTS)
        for (const Json &row : stand["sticks"].items) {
            States states(v.config, fixture["walk"]["dt_s"].f());
            states.enter(states.stand);
            states.hold(controller(0, 0, 0, 0), ticks);
            const Pose rest = poseOverFeet(fixture, bodyFrameFeet(v.config, states.body));
            const Json &stick = row["stick"];
            states.hold(controller(stick["lx"].f(), stick["ly"].f(), stick["rx"].f(), stick["ry"].f()), ticks);
            const Pose held = poseOverFeet(fixture, bodyFrameFeet(v.config, states.body));
            const std::string where = std::string(v.name) + ", " + row["name"].text;
            for (int axis = 0; axis < 2; axis++)
                CHECK_SIGN(row["shift_sign"][axis].i(),
                           sign(held.offset[axis] - rest.offset[axis], stand["zero_m"].f()),
                           where + (axis ? ", shift left" : ", shift forward"));
            checkRotation(row["rotation_sign"], rest, held, stand["zero_rad"].f(), where);
        }
}

void walkSticksMoveTheStanceFeetWithTheFixtureSigns(const Json &fixture) {
    const Json &walk = fixture["walk"];
    CHECK(walk["commands"].size() == 3);
    for (const Variant &v : VARIANTS)
        for (const Json &row : walk["commands"].items) {
            States states(v.config, walk["dt_s"].f());
            states.enter(states.stand);
            states.hold(controller(0, 0, 0, 0), walk["settle_ticks"].i());
            states.enter(states.walk);
            const Json &stick = row["stick"];
            states.hold(controller(stick["lx"].f(), stick["ly"].f(), stick["rx"].f(), 0), walk["settle_ticks"].i());
            float sum[4][2] = {};
            int planted[4] = {};
            for (int i = 0; i < walk["ticks"].i(); i++) {
                const std::array<bool, 4> before = states.walk.stance();
                const std::vector<Rep> from = bodyFrameFeet(v.config, states.body);
                states.tick();
                const std::array<bool, 4> after = states.walk.stance();
                const std::vector<Rep> to = bodyFrameFeet(v.config, states.body);
                for (int leg = 0; leg < 4; leg++) {
                    if (!before[leg] || !after[leg]) continue;
                    sum[leg][0] += to[leg].forward - from[leg].forward;
                    sum[leg][1] += to[leg].left - from[leg].left;
                    planted[leg]++;
                }
            }
            for (int leg = 0; leg < 4; leg++) {
                const std::string where =
                    std::string(v.name) + ", " + row["name"].text + ", " + fixture["legs"][leg]["name"].text;
                CHECK(planted[leg] > 0);
                for (int axis = 0; axis < 2; axis++)
                    CHECK_SIGN(row["stance_foot_sign"][leg][axis].i(),
                               sign(sum[leg][axis] / std::max(planted[leg], 1), walk["zero_m_per_tick"].f()),
                               where + (axis ? ", left" : ", forward"));
            }
        }
}

constexpr int64_t TICK_US = 10000;

void tick(MotionService &motion, int ticks = 1) {
    for (int i = 0; i < ticks; i++) {
        fake_timer::nowUs += TICK_US;
        motion.update(ImuSample {}, eGestureNone);
    }
}

void input(MotionService &motion, const socket_message_ControllerData &data) {
    motion.inbox.postInput(data, fake_timer::nowUs / 1000, fake_timer::nowUs);
    tick(motion);
}

void standingRobot(MotionService &motion, const KinConfig &config, int ticks) {
    motion.useConfig(&config);
    motion.begin();
    motion.inbox.postMode(socket_message_ModesEnum_STAND);
    tick(motion);
    input(motion, controller(0, 0, 0, 0));
    tick(motion, ticks);
}

// A stick held as the app holds it: sent again every resend_ticks, which keeps the dead-man stop away.
void hold(MotionService &motion, const socket_message_ControllerData &data, int ticks, int resendTicks) {
    for (int i = 0; i < ticks; i += resendTicks) {
        input(motion, data);
        tick(motion, std::min(resendTicks, ticks - i) - 1);
    }
}

void walkSticksMoveTheOdometryWithTheFixtureSigns(const Json &fixture) {
    const Json &walk = fixture["walk"];
    const int resend = walk["resend_ticks"].i();
    for (const Variant &v : VARIANTS)
        for (const Json &row : walk["commands"].items) {
            MotionService motion;
            standingRobot(motion, v.config, walk["settle_ticks"].i());
            motion.inbox.postMode(socket_message_ModesEnum_WALK);
            const Json &stick = row["stick"];
            const socket_message_ControllerData held = controller(stick["lx"].f(), stick["ly"].f(), stick["rx"].f(), 0);
            hold(motion, held, walk["settle_ticks"].i(), resend);
            motion.resetOdometry();
            hold(motion, held, walk["ticks"].i(), resend);
            const FusedEstimate at = motion.odometry();
            const std::string where = std::string(v.name) + ", " + row["name"].text;
            const Json &expected = row["odometry_sign"];
            CHECK_SIGN(expected[0].i(), sign(at.x, walk["zero_m"].f()), where + ", odometry x");
            CHECK_SIGN(expected[1].i(), sign(at.y, walk["zero_m"].f()), where + ", odometry y");
            CHECK_SIGN(expected[2].i(), sign(at.yaw, walk["zero_rad"].f()), where + ", odometry yaw");
            CHECK_SIGN(expected[2].i(), sign(at.kinematicYaw, walk["zero_rad"].f()), where + ", kinematic yaw");
        }
}

void goalsWalkWithTheFixtureSticksAndEndOnTheirSide(const Json &fixture) {
    const Json &goals = fixture["goals"];
    CHECK(goals["moves"].size() == 3);
    for (const Json &row : goals["moves"].items) {
        MotionService motion;
        standingRobot(motion, KIN_CONFIG_SPOTMICRO_ESP32_MINI, fixture["walk"]["settle_ticks"].i());
        const Json &goal = row["goal"];
        motion.inbox.postGoal({goal["forward_m"].f(), goal["sideways_m"].f(),
                               goal["turn_deg"].f() * static_cast<float>(M_PI) / 180, 0.5f});
        tick(motion);
        const CommandMsg &command = motion.currentCommand();
        const Json &sticks = row["stick_sign"];
        const float zero = goals["zero_stick"].f();
        CHECK_SIGN(sticks["lx"].i(), sign(command.lx, zero), row["name"].text + ", lx");
        CHECK_SIGN(sticks["ly"].i(), sign(command.ly, zero), row["name"].text + ", ly");
        CHECK_SIGN(sticks["rx"].i(), sign(command.rx, zero), row["name"].text + ", rx");
        for (int i = 0; i < 3000 && motion.goalStatus().state == GoalState::RUNNING; i++) tick(motion);
        CHECK(motion.goalStatus().state == GoalState::REACHED);
        const FusedEstimate at = motion.odometry();
        const Json &expected = row["odometry_sign"];
        CHECK_SIGN(expected[0].i(), sign(at.x, goals["zero_m"].f()), row["name"].text + ", odometry x");
        CHECK_SIGN(expected[1].i(), sign(at.y, goals["zero_m"].f()), row["name"].text + ", odometry y");
        CHECK_SIGN(expected[2].i(), sign(at.yaw, goals["zero_rad"].f()), row["name"].text + ", odometry yaw");
    }
}

} // namespace

int main(int argc, char **argv) {
    const Json fixture = readFixture(argc > 1 ? argv[1] : "sign_convention.json");
    if (fixture.kind == Json::OBJECT) {
        legsSitOnTheirFixtureSides(fixture);
        attitudesRotateTheBodyWithTheFixtureSigns(fixture);
        standSticksShiftAndRotateTheBodyWithTheFixtureSigns(fixture);
        walkSticksMoveTheStanceFeetWithTheFixtureSigns(fixture);
        walkSticksMoveTheOdometryWithTheFixtureSigns(fixture);
        goalsWalkWithTheFixtureSticksAndEndOnTheirSide(fixture);
    }
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
