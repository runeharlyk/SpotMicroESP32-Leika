#pragma once

#include <atomic>
#include <communication/comm_base.hpp>
#include <communication/ble_frame.h>

/**
 * The app's protocol over a Bluetooth LE Nordic UART Service, whatever stack runs the radio: the robot takes one
 * central, client id 0, and frames messages as ble_frame.h does. A stack subclasses it, reports connections and the
 * chunks the central writes, and sends notifications. NimbleLink is the one the firmware runs.
 */
class BleLink : public CommAdapterBase {
  public:
    BleLink() : decoder_([this](const uint8_t *message, size_t len) { handleIncoming(message, len, 0); }) {}

    bool connected() const { return connected_; }

    uint32_t session(int cid) override { return session_; }

  protected:
    void onConnected() {
        session_++;
        connected_ = true;
    }

    void onDisconnected() {
        connected_ = false;
        removeClient(0);
    }

    /** One chunk the central wrote; on one task only, the one that also calls resetReceiver. */
    void received(const uint8_t *chunk, size_t len) { decoder_.feed(chunk, len); }

    /** Forgets a partial message from a connection that ended. */
    void resetReceiver() { decoder_.reset(); }

    /** The largest notification the connection takes now: its ATT MTU less the 3-byte ATT header. */
    virtual size_t chunkSize() = 0;

    /** Sends one notification; false when it could not be sent. */
    virtual bool notify(const uint8_t *chunk, size_t len) = 0;

    /** Ends the connection: the central would otherwise read the next message as the rest of a cut-off one. */
    virtual void dropConnection() = 0;

  private:
    std::atomic<bool> connected_ {false};
    std::atomic<uint32_t> session_ {0};
    ble_frame::Decoder decoder_;

    bool isSession(int cid, uint32_t session) override { return connected_ && session == session_; }

    bool ready(int cid) override { return connected_; }

    // Every sender holds CommAdapterBase's encode mutex, so the chunks of two messages never interleave.
    bool send(const uint8_t *data, size_t len, int cid) override {
        if (!connected_) return false;
        const auto chunks = ble_frame::encode(data, len, chunkSize());
        if (chunks.empty()) {
            ESP_LOGE("BleLink", "Message of %u bytes is too large for a frame", len);
            return false;
        }
        for (size_t i = 0; i < chunks.size(); i++) {
            if (notify(chunks[i].data(), chunks[i].size())) continue;
            if (i > 0) dropConnection();
            return false;
        }
        return true;
    }
};
