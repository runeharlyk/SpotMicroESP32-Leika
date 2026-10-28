// Host test of ota_download.h, built and run by test_host_programs.py.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <ota_download.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// The next app slot, recording what the session did to it.
struct FakeFlash : OtaFlash {
    uint32_t slot = 1 << 16;
    std::vector<uint8_t> written;
    int aborts = 0;
    bool active = false;

    uint32_t capacity() override { return slot; }
    bool begin() override {
        written.clear();
        return true;
    }
    bool write(const uint8_t *data, size_t size) override {
        written.insert(written.end(), data, data + size);
        return true;
    }
    bool end() override { return true; }
    bool activate() override { return active = true; }
    void abort() override { aborts++; }
};

// A server: its answer to the request, the length it announces, and the body it sends in pieces of at most `piece`
// bytes, failing after `breakAfter` bytes if that is set; `beforeRead` runs before each read, as time passing would.
struct FakeServer : OtaSource {
    int status = 200;
    int64_t announced = -1;
    std::vector<uint8_t> body;
    size_t piece = 1000, sent = 0, breakAfter = SIZE_MAX;
    std::string requested;
    int opens = 0, closes = 0;
    void (*beforeRead)(FakeServer &) = nullptr;

    int open(const char *url, int64_t &length) override {
        opens++;
        requested = url;
        length = announced;
        return status;
    }
    int read(uint8_t *buffer, size_t size) override {
        if (beforeRead) beforeRead(*this);
        if (sent >= breakAfter) return -1;
        const size_t n = std::min({size, piece, body.size() - sent, breakAfter - sent});
        std::memcpy(buffer, body.data() + sent, n);
        sent += n;
        return static_cast<int>(n);
    }
    void close() override { closes++; }
};

static constexpr char URL[] = "https://runeharlyk.github.io/SpotMicroESP32-Leika/flash/firmware/esp32-s3-n8r2.bin";

// An application image as far as the session looks: the image magic, then the app descriptor's magic, then payload.
static std::vector<uint8_t> appImage(size_t size) {
    std::vector<uint8_t> image(size);
    for (size_t i = 0; i < size; i++) image[i] = static_cast<uint8_t>(i * 7);
    image[0] = ota_image::IMAGE_MAGIC;
    for (int b = 0; b < 4; b++) image[ota_image::APP_DESC_OFFSET + b] = (ota_image::APP_DESC_MAGIC >> (8 * b)) & 0xFF;
    return image;
}

static uint32_t clockMs = 0;
static uint32_t now() { return clockMs; }
static bool deactivated() { return true; }
static bool activated() { return false; }

struct Rig {
    FakeFlash flash;
    OtaSession session {flash};
    FakeServer server;
    OtaDownload download {session, server};

    explicit Rig(size_t size = 10000) {
        server.body = appImage(size);
        server.announced = static_cast<int64_t>(size);
    }
};

static void onlyAnHttpsUrlWithAHostIsTaken() {
    CHECK(OtaDownload::urlError(URL) == nullptr);
    CHECK(OtaDownload::urlError("https://192.168.1.10:8443/fw.bin") == nullptr);
    CHECK(OtaDownload::urlError("http://example.com/fw.bin") != nullptr);
    CHECK(OtaDownload::urlError("HTTPS://example.com/fw.bin") != nullptr);
    CHECK(OtaDownload::urlError("ftp://example.com/fw.bin") != nullptr);
    CHECK(OtaDownload::urlError("") != nullptr);
    CHECK(OtaDownload::urlError("https://") != nullptr);
    CHECK(OtaDownload::urlError("https:///fw.bin") != nullptr);
    CHECK(OtaDownload::urlError("https://@evil/fw.bin") != nullptr);
    CHECK(OtaDownload::urlError("https://example.com/a b.bin") != nullptr);
    CHECK(OtaDownload::urlError("https://example.com/\x01.bin") != nullptr);
    std::string longest = "https://example.com/" + std::string(OtaDownload::URL_MAX - 20, 'a');
    CHECK(OtaDownload::urlError(longest.c_str()) == nullptr);
    longest += 'a';
    CHECK(OtaDownload::urlError(longest.c_str()) != nullptr);
}

// The image arrives in the server's pieces, through the session into the slot, which is then selected; the status
// says how far it got all the way.
static void aWholeImageIsDownloadedIntoTheSlotAndSelected() {
    Rig rig;
    CHECK(rig.download.claim(URL, true).status == 202);
    CHECK(rig.download.busy());
    CHECK(rig.download.status().state == OtaDownloadState::DOWNLOADING);
    rig.download.run(deactivated, now);
    CHECK(rig.server.requested == URL);
    CHECK(rig.flash.written == rig.server.body);
    CHECK(rig.flash.active && rig.flash.aborts == 0);
    const OtaDownload::Status status = rig.download.status();
    CHECK(status.state == OtaDownloadState::DOWNLOADED);
    CHECK(status.written == 10000 && status.size == 10000);
    CHECK(!rig.download.busy() && !rig.session.running());
    CHECK(rig.server.closes >= 1);
}

static void aRequestTheRobotCannotTakeIsRefusedUpFront() {
    Rig rig;
    CHECK(rig.download.claim("http://example.com/fw.bin", true).status == 422);
    CHECK(rig.download.claim(URL, false).status == 409);
    CHECK(!rig.download.busy());
    CHECK(rig.download.claim(URL, true).status == 202);
    CHECK(rig.download.claim(URL, true).status == 409);
    CHECK(rig.server.opens == 0);
}

// Upload and download share the one session: neither starts while the other runs, and neither's client reaches into
// the other's update.
static void anUploadAndADownloadExcludeEachOther() {
    {
        Rig rig;
        CHECK(rig.session.start(5000, true, 3, 0).status == 200);
        CHECK(rig.download.claim(URL, true).status == 409);
    }
    {
        Rig rig;
        static OtaSession *session;
        session = &rig.session;
        rig.server.beforeRead = [](FakeServer &) {
            CHECK(session->start(5000, true, 3, 0).status == 409);
            CHECK(session->chunk(0, nullptr, 0, 3, 0).status == 409);
            session->drop(3);
        };
        CHECK(rig.download.claim(URL, true).status == 202);
        rig.download.run(deactivated, now);
        CHECK(rig.download.status().state == OtaDownloadState::DOWNLOADED);
        CHECK(rig.flash.written == rig.server.body);
    }
}

static void expectFailure(Rig &rig, const char *reason) {
    const OtaDownload::Status status = rig.download.status();
    if (status.state != OtaDownloadState::FAILED || std::strstr(status.error, reason) == nullptr)
        std::printf("expected a failure with \"%s\", got state %d \"%s\"\n", reason, static_cast<int>(status.state),
                    status.error);
    CHECK(status.state == OtaDownloadState::FAILED);
    CHECK(std::strstr(status.error, reason) != nullptr);
    CHECK(!rig.flash.active);
    CHECK(!rig.download.busy() && !rig.session.running());
    CHECK(rig.server.closes >= 1);
}

// Every way a download can go wrong leaves the slot unselected, the session free and the reason in the status.
static void aFailedDownloadSelectsNothingAndSaysWhy() {
    {
        Rig rig;
        rig.server.status = 404;
        rig.download.claim(URL, true);
        rig.download.run(deactivated, now);
        expectFailure(rig, "404");
    }
    {
        Rig rig;
        rig.server.status = 0;
        rig.download.claim(URL, true);
        rig.download.run(deactivated, now);
        expectFailure(rig, "Could not reach");
    }
    {
        Rig rig;
        rig.server.announced = -1;  // chunked, without a Content-Length
        rig.download.claim(URL, true);
        rig.download.run(deactivated, now);
        expectFailure(rig, "size");
    }
    {
        Rig rig;
        rig.server.announced = rig.flash.slot + 1;
        rig.download.claim(URL, true);
        rig.download.run(deactivated, now);
        expectFailure(rig, "does not fit");
    }
    {
        Rig rig;
        rig.server.body[0] = 0;  // a factory image starts with the bootloader, not the app's header
        rig.download.claim(URL, true);
        rig.download.run(deactivated, now);
        expectFailure(rig, "Not an application image");
        CHECK(rig.flash.aborts == 1);
    }
    {
        Rig rig;
        rig.server.breakAfter = 4000;
        rig.download.claim(URL, true);
        rig.download.run(deactivated, now);
        expectFailure(rig, "broke off");
        CHECK(rig.flash.aborts == 1);
        CHECK(rig.download.status().written == 4000);
    }
    {
        Rig rig;
        rig.server.body.resize(6000);  // the server closed the connection early
        rig.download.claim(URL, true);
        rig.download.run(deactivated, now);
        expectFailure(rig, "incomplete");
    }
    {
        // Activated during the TLS handshake, after the request was answered.
        Rig rig;
        rig.download.claim(URL, true);
        rig.download.run(activated, now);
        expectFailure(rig, "Deactivate first");
        CHECK(rig.flash.written.empty());
    }
    {
        // A server that stalls past the session's timeout: the service task expires the session under the download.
        Rig rig;
        static OtaSession *session;
        session = &rig.session;
        clockMs = 0;
        rig.server.beforeRead = [](FakeServer &server) {
            if (server.sent < 3000) return;
            clockMs += OtaSession::TIMEOUT_MS + 1;
            session->expire(clockMs);
        };
        rig.download.claim(URL, true);
        rig.download.run(deactivated, now);
        expectFailure(rig, "No update running");
    }
}

// After a failure the robot takes the next request.
static void aFailedDownloadCanBeTriedAgain() {
    Rig rig;
    rig.server.status = 503;
    rig.download.claim(URL, true);
    rig.download.run(deactivated, now);
    CHECK(rig.download.status().state == OtaDownloadState::FAILED);
    rig.server.status = 200;
    CHECK(rig.download.claim(URL, true).status == 202);
    CHECK(rig.download.status().error[0] == '\0');
    rig.download.run(deactivated, now);
    CHECK(rig.download.status().state == OtaDownloadState::DOWNLOADED);
    CHECK(rig.flash.written == rig.server.body);
}

int main() {
    onlyAnHttpsUrlWithAHostIsTaken();
    aWholeImageIsDownloadedIntoTheSlotAndSelected();
    aRequestTheRobotCannotTakeIsRefusedUpFront();
    anUploadAndADownloadExcludeEachOther();
    aFailedDownloadSelectsNothingAndSaysWhy();
    aFailedDownloadCanBeTriedAgain();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
