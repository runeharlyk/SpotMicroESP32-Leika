#pragma once

#include <ota_session.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

/** An image fetched over HTTPS: esp_http_client on the robot (ota_http.h), a fake in the host tests. */
class OtaSource {
  public:
    virtual ~OtaSource() = default;
    /**
     * Requests `url`, following redirects, and returns the final answer's HTTP status; `length` is the image's size,
     * or negative when the server did not say. A status of 0 is a connection or TLS failure.
     */
    virtual int open(const char *url, int64_t &length) = 0;
    /** The next bytes of the body: how many, 0 at its end, negative on failure. */
    virtual int read(uint8_t *buffer, size_t size) = 0;
    virtual void close() = 0;
};

/** The values of the protocol's OtaDownloadState. */
enum class OtaDownloadState : uint8_t { IDLE, DOWNLOADING, DOWNLOADED, FAILED };

/**
 * The robot fetches an app image from an https URL itself and writes it through the same OtaSession as an upload
 * from the app, under a client of its own: the slot, the factory-image check, the validation and the timeout are the
 * session's, and while it runs the session refuses an upload, as an upload refuses the download. Plain http is
 * refused: an unauthenticated image over a hostile network would be a flash write anyone on it could make.
 * claim() runs on the socket's task and answers the request; run() then does the download on a task of its own.
 */
class OtaDownload {
  public:
    static constexpr int CLIENT = -2;     // socket clients count from 0, and -1 is a link that never leaves
    static constexpr size_t URL_MAX = 255; // platform_shared/message.options
    static constexpr size_t ERROR_MAX = 63;

    struct Status {
        OtaDownloadState state = OtaDownloadState::IDLE;
        uint32_t written = 0, size = 0;
        char error[ERROR_MAX + 1] = "";
    };

    OtaDownload(OtaSession &session, OtaSource &source) : session_(session), source_(source) {}

    /** Why `url` is not one to download from, or nullptr. */
    static const char *urlError(const char *url) {
        static constexpr char SCHEME[] = "https://";
        const size_t length = strnlen(url, URL_MAX + 1);
        if (length > URL_MAX) return "The URL is too long";
        if (strncmp(url, SCHEME, sizeof(SCHEME) - 1) != 0) return "The URL must start with https://";
        for (size_t i = 0; i < length; i++)
            if (url[i] <= ' ' || url[i] == 0x7f) return "The URL holds a space or a control character";
        const char host = url[sizeof(SCHEME) - 1];
        if (host == '\0' || host == '/' || host == ':' || host == '@') return "The URL names no host";
        return nullptr;
    }

    /** Takes the download on for `url`, or refuses it with the reason; run() is the next call after a 202. */
    OtaReply claim(const char *url, bool deactivated) {
        if (const char *error = urlError(url)) return {422, error};
        if (!deactivated) return {409, "Deactivate first"};
        if (session_.running() || busy_.exchange(true)) return {409, "An update is already running"};
        std::strncpy(url_, url, URL_MAX);
        url_[URL_MAX] = '\0';
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = {};
        status_.state = OtaDownloadState::DOWNLOADING;
        return {202, ""};
    }

    /**
     * Downloads the claimed URL into the update slot. `deactivated` is asked again once the image's size is known,
     * since the request was answered a TLS handshake ago; `nowMs` feeds the session's timeout.
     */
    template <typename Deactivated, typename Clock>
    void run(Deactivated deactivated, Clock nowMs) {
        int64_t length = -1;
        const int status = source_.open(url_, length);
        if (status != 200) {
            char reason[ERROR_MAX + 1];
            if (status == 0)
                std::strncpy(reason, "Could not reach the server", sizeof(reason));
            else
                std::snprintf(reason, sizeof(reason), "The server answered %d", status);
            return fail(reason);
        }
        if (length <= 0 || length > UINT32_MAX) return fail("The server did not give the image's size");
        const OtaReply started = session_.start(static_cast<uint32_t>(length), deactivated(), CLIENT, nowMs());
        if (started.status != 200) return fail(started.reason);
        record(0, static_cast<uint32_t>(length));
        uint32_t written = 0;
        for (uint32_t index = 0;; index++) {
            const int got = source_.read(buffer_, sizeof(buffer_));
            if (got < 0) {
                session_.drop(CLIENT);
                return fail("The download broke off");
            }
            if (got == 0) break;
            const OtaReply wrote = session_.chunk(index, buffer_, static_cast<size_t>(got), CLIENT, nowMs());
            if (wrote.status != 200) return fail(wrote.reason);
            written += static_cast<uint32_t>(got);
            record(written, static_cast<uint32_t>(length));
        }
        const OtaReply finished = session_.finish(CLIENT);
        if (finished.status != 200) return fail(finished.reason);
        source_.close();
        std::lock_guard<std::mutex> lock(mutex_);
        status_.state = OtaDownloadState::DOWNLOADED;
        busy_ = false;
    }

    /** Ends a claimed download that never ran, with the reason. */
    void abandon(const char *reason) { fail(reason); }

    Status status() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }

    /** From claim() until run() ends; lock-free, as OtaSession::running() is. */
    bool busy() const { return busy_; }

  private:
    void record(uint32_t written, uint32_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.written = written;
        status_.size = size;
    }

    void fail(const char *reason) {
        source_.close();
        std::lock_guard<std::mutex> lock(mutex_);
        status_.state = OtaDownloadState::FAILED;
        std::strncpy(status_.error, reason, ERROR_MAX);
        status_.error[ERROR_MAX] = '\0';
        busy_ = false;
    }

    OtaSession &session_;
    OtaSource &source_;
    mutable std::mutex mutex_;
    Status status_;
    std::atomic<bool> busy_ = false;
    char url_[URL_MAX + 1] = "";
    // One TLS record's worth; the session takes chunks of any size up to what was announced.
    uint8_t buffer_[4096];
};
