#pragma once

// A CommAdapterBase on a fake transport that decodes what it sends, for the host tests of subscriptions and state
// topics (topic_snapshot_test.cpp).

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <pb_decode.h>
#include <pb_encode.h>
#include <communication/comm_base.hpp>

struct Frame {
    int cid;
    socket_message_Message message;
};

// A link whose clients can take a frame unless `full` names them; it keeps every frame it sent, decoded, with its
// client. While `sendsFail`, a client looks ready but every send fails, as a BLE notify without an mbuf or a serial
// write cut short does, and so do the next `failNext` sends.
class FakeLink final : public CommAdapterBase {
  public:
    template <size_t N>
    explicit FakeLink(const Topic (&topics)[N]) {
        setTopics(topics);
    }

    std::set<int> full;
    std::atomic<bool> sendsFail {false};
    std::atomic<int> failNext {0};

    bool receive(const socket_message_Message& message, int cid) {
        size_t size = 0;
        if (!pb_get_encoded_size(&size, socket_message_Message_fields, &message)) return false;
        std::vector<uint8_t> bytes(size);
        pb_ostream_t stream = pb_ostream_from_buffer(bytes.data(), bytes.size());
        if (!pb_encode(&stream, socket_message_Message_fields, &message)) return false;
        handleIncoming(bytes.data(), stream.bytes_written, cid);
        return true;
    }

    void subscribeTo(int32_t tag, int cid) {
        socket_message_Message message = socket_message_Message_init_zero;
        message.which_message = socket_message_Message_sub_notif_tag;
        message.message.sub_notif.tag = tag;
        receive(message, cid);
    }

    void unsubscribeFrom(int32_t tag, int cid) {
        socket_message_Message message = socket_message_Message_init_zero;
        message.which_message = socket_message_Message_unsub_notif_tag;
        message.message.unsub_notif.tag = tag;
        receive(message, cid);
    }

    std::vector<Frame> to(int cid) {
        std::lock_guard<std::mutex> lock(sentMutex_);
        std::vector<Frame> frames;
        for (const Frame& frame : sent_)
            if (frame.cid == cid) frames.push_back(frame);
        return frames;
    }

    size_t sentCount() {
        std::lock_guard<std::mutex> lock(sentMutex_);
        return sent_.size();
    }

    void forget() {
        std::lock_guard<std::mutex> lock(sentMutex_);
        sent_.clear();
    }

  protected:
    bool ready(int cid) override { return !full.count(cid); }
    bool send(const uint8_t* data, size_t len, int cid) override {
        if (sendsFail) return false;
        if (failNext > 0) {
            failNext--;
            return false;
        }
        Frame frame {cid, socket_message_Message_init_zero};
        pb_istream_t stream = pb_istream_from_buffer(data, len);
        if (!pb_decode(&stream, socket_message_Message_fields, &frame.message)) return false;
        std::lock_guard<std::mutex> lock(sentMutex_);
        sent_.push_back(frame);
        return true;
    }

  private:
    std::mutex sentMutex_;
    std::vector<Frame> sent_;
};
