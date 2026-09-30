// Host test of communication/ws_origin.h, built and run by test_host_programs.py.
#include <cstdio>
#include <communication/ws_origin.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// The embedded app, however the robot was reached: its IP, its mDNS name, or its access point.
static void theAppTheRobotServesMayConnect() {
    CHECK(wsOriginAllowed("http://192.168.1.39", "192.168.1.39"));
    CHECK(wsOriginAllowed("http://spot-micro-27cb70.local", "spot-micro-27cb70.local"));
    CHECK(wsOriginAllowed("http://Spot-Micro-27CB70.local", "spot-micro-27cb70.local"));
    CHECK(wsOriginAllowed("http://192.168.4.1:80", "192.168.4.1:80"));
}

// pnpm dev and vite preview serve the app from the developer's own machine.
static void theAppServedLocallyForDevelopmentMayConnect() {
    CHECK(wsOriginAllowed("http://localhost:5173", "192.168.1.39"));
    CHECK(wsOriginAllowed("http://127.0.0.1:4173", "192.168.1.39"));
    CHECK(wsOriginAllowed("http://[::1]:5173", "192.168.1.39"));
    CHECK(wsOriginAllowed("http://localhost", "192.168.1.39"));
}

// Scripts and tools are not browsers and send no Origin; a page cannot omit it.
static void aClientWithoutAnOriginMayConnect() { CHECK(wsOriginAllowed(nullptr, "192.168.1.39")); }

// Any other page open on the LAN could otherwise drive the robot or read its settings.
static void anyOtherPageMayNot() {
    CHECK(!wsOriginAllowed("http://evil.example", "192.168.1.39"));
    CHECK(!wsOriginAllowed("https://runeharlyk.github.io", "192.168.1.39"));
    CHECK(!wsOriginAllowed("null", "192.168.1.39"));
    CHECK(!wsOriginAllowed("", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://192.168.1.39.evil.example", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://192.168.1.3", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://localhost.evil.example", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://localhost:5173.evil.example", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://localhost:", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://192.168.1.39", nullptr));
    CHECK(!wsOriginAllowed("ftp://192.168.1.39", "192.168.1.39"));
}

int main() {
    theAppTheRobotServesMayConnect();
    theAppServedLocallyForDevelopmentMayConnect();
    aClientWithoutAnOriginMayConnect();
    anyOtherPageMayNot();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
