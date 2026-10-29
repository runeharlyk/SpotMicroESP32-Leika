#pragma once

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdlib>
#include <functional>
#include <list>
#include <map>
#include <utility>
#include <vector>
#include <type_traits>
#include <communication/proto_helpers.h>
#include <communication/topics.h>

class CommAdapterBase;

// Where a state topic's snapshot puts its owner's current value (communication/topics.h): to one client, as a
// subscribe's answer or a resend, or to every subscriber, as a push. Only the link makes one, with its encode lock
// held, so a snapshot sends nowhere else and at no other time.
class SnapshotSink {
  public:
    template <typename T>
    void put(const T& value);

  private:
    friend class CommAdapterBase;
    SnapshotSink(CommAdapterBase& link, int cid) : link_(link), cid_(cid) {}
    SnapshotSink(const SnapshotSink&) = delete;
    SnapshotSink& operator=(const SnapshotSink&) = delete;

    CommAdapterBase& link_;
    int cid_;
    bool delivered_ = false;
};

class CommAdapterBase {
  public:
    CommAdapterBase() {
        mutex_ = xSemaphoreCreateMutex();
        encode_mutex_ = xSemaphoreCreateMutex();
        decoder_.onSubscribe([this](int32_t tag, int cid) { subscribe(tag, cid); });
        decoder_.onUnsubscribe([this](int32_t tag, int cid) { unsubscribe(tag, cid); });
        decoder_.onPing([this](int cid) { sendPong(cid); });
    }
    ~CommAdapterBase() {
        vSemaphoreDelete(mutex_);
        vSemaphoreDelete(encode_mutex_);
    }

    virtual void begin() {}

    bool hasSubscribers(int32_t tag) {
        ScopedLock lock(mutex_);
        auto it = client_subscriptions_.find(tag);
        return it != client_subscriptions_.end() && !it->second.empty();
    }

    ProtoDecoder& decoder() { return decoder_; }

    template <typename T>
    void on(std::function<void(const T&, int)> handler) {
        decoder_.on<T>(handler);
    }

    /** The topics this link serves; a tag not in the table is served as a stream. */
    template <size_t N>
    void setTopics(const Topic (&topics)[N]) {
        topics_ = topics;
        topicCount_ = N;
    }

    /** Whether every addressed client got the message. */
    template <typename T>
    bool emit(const T& data, int clientId = -1) {
        if (clientId < 0 && !hasSubscribers(MessageTraits<T>::tag)) return true;
        // Tasks other than the link's emit too (the service loop, deferred replies); msg_ and the encode buffer are
        // shared, and socket_message_Message is over a kilobyte.
        ScopedLock lock(encode_mutex_);
        return encodeAndSend(data, clientId, false);
    }

    /**
     * Sends a state topic's current value to its subscribers, read by its snapshot under the encode lock that every
     * subscribe's snapshot takes too: a push never carries a value older than one a subscribe answered with before it.
     * Whether every subscriber got it; one that did not is marked for resendDirty.
     */
    bool push(int32_t tag) {
        const Topic* topic = topicOf(tag);
        if (!topic || topic->kind != TopicKind::STATE || !hasSubscribers(tag)) return true;
        ScopedLock lock(encode_mutex_);
        SnapshotSink sink(*this, -1);
        topic->snapshot(sink);
        return sink.delivered_;
    }

    /**
     * From the service loop: a state push a client could not take marked its subscription dirty, and each dirty client
     * that can take a frame now gets the topic's current value instead, once. A client that still cannot keeps its mark
     * for the next pass, so the service loop never waits on one.
     */
    void resendDirty() {
        std::vector<std::pair<int32_t, int>> due;
        {
            ScopedLock lock(mutex_);
            for (const auto& [tag, clients] : client_subscriptions_)
                for (const Subscriber& client : clients)
                    if (client.dirty) due.emplace_back(tag, client.cid);
        }
        if (due.empty()) return;
        ScopedLock lock(encode_mutex_);
        for (const auto& [tag, cid] : due) {
            const Topic* topic = topicOf(tag);
            if (!topic || topic->kind != TopicKind::STATE || !ready(cid) || !clearDirty(tag, cid)) continue;
            SnapshotSink sink(*this, cid);
            topic->snapshot(sink);
            if (!sink.delivered_) markDirty(tag, cid, true);
        }
    }

    /** Called on the link's task after a client subscribes to a tag, before its answer. */
    void onSubscribed(std::function<void(int32_t tag, int cid)> listener) { subscribedListener_ = std::move(listener); }

    /** Called on the link's task for every frame a client sends, before it is handled. */
    void onHeard(std::function<void(int cid)> listener) { heardListener_ = std::move(listener); }

    /** Called once a client has gone: its connection closed, or the host behind it stopped reading. */
    void onClientGone(std::function<void(int cid)> listener) { goneListener_ = std::move(listener); }

    /** The connection a client id stands for now, for a reply sent later; a link with one lasting client has one. */
    virtual uint32_t session(int cid) { return 1; }

    /** Emits only while `cid` is still the connection `session` was taken from. */
    template <typename T>
    void emitToSession(const T& data, int cid, uint32_t session) {
        if (isSession(cid, session)) emit(data, cid);
    }

  protected:
    virtual bool send(const uint8_t* data, size_t len, int cid) = 0;

    virtual bool isSession(int cid, uint32_t session) { return true; }

    /** Whether a client can take a frame now, without waiting. */
    virtual bool ready(int cid) { return true; }

    /** Sends a stream's frame only if the client can take it now: a stream never waits on a client. */
    virtual bool sendStream(const uint8_t* data, size_t len, int cid) { return ready(cid) && send(data, len, cid); }

    // A client resubscribes on every reconnect, so a subscribe it already holds adds nothing; it is answered again all
    // the same. A state topic is answered with its snapshot, then every subscribe with its ack
    // (communication/topics.h), both under encode_mutex_, which every push to this link takes, so a push made after the
    // snapshot follows it. The answer goes out as a stream's frame does, never waiting on the client: one that cannot
    // take it now gets neither snapshot nor ack, its state topic is marked for resendDirty, and the client, without its
    // ack, subscribes again.
    void subscribe(int32_t tag, int cid = 0) {
        {
            ScopedLock lock(mutex_);
            auto& clients = client_subscriptions_[tag];
            if (!findClient(clients, cid)) clients.push_back({cid, false});
        }
        ESP_LOGD("ProtoComm", "Client %d subscribed to tag %d", cid, (int)tag);
        if (subscribedListener_) subscribedListener_(tag, cid);

        ScopedLock lock(encode_mutex_);
        const Topic* topic = topicOf(tag);
        const bool state = topic && topic->kind == TopicKind::STATE;
        if (!ready(cid)) {
            if (state) markDirty(tag, cid, true);
            return;
        }
        if (state) {
            SnapshotSink sink(*this, cid);
            topic->snapshot(sink);
            if (!sink.delivered_) return markDirty(tag, cid, true);
        }
        msg_.which_message = socket_message_Message_sub_notif_tag;
        msg_.message.sub_notif.tag = tag;
        sendSmall(cid, true);
    }

    void unsubscribe(int32_t tag, int cid = 0) {
        {
            ScopedLock lock(mutex_);
            auto it = client_subscriptions_.find(tag);
            if (it != client_subscriptions_.end())
                it->second.remove_if([cid](const Subscriber& client) { return client.cid == cid; });
        }
        ESP_LOGI("ProtoComm", "Client %d unsubscribed from tag %d", cid, (int)tag);
    }

    void removeClient(int cid) {
        ScopedLock lock(mutex_);
        for (auto& [tag, clients] : client_subscriptions_)
            clients.remove_if([cid](const Subscriber& client) { return client.cid == cid; });
    }

    void clientGone(int cid) {
        removeClient(cid);
        if (goneListener_) goneListener_(cid);
    }

    void handleIncoming(const uint8_t* data, size_t len, int cid) {
        if (heardListener_) heardListener_(cid);
        if (!decoder_.decode(data, len, cid)) {
            ESP_LOGE("ProtoComm", "Failed to decode incoming message from client %d", cid);
        }
    }

    void sendPong(int cid) {
        ScopedLock lock(encode_mutex_);
        msg_.which_message = socket_message_Message_pongmsg_tag;
        msg_.message.pongmsg = socket_message_PongMsg_init_zero;
        sendSmall(cid, false);
    }

    ProtoDecoder decoder_;

  private:
    friend class SnapshotSink;

    class ScopedLock {
      public:
        explicit ScopedLock(SemaphoreHandle_t mutex) : mutex_(mutex) { xSemaphoreTake(mutex_, portMAX_DELAY); }
        ~ScopedLock() { xSemaphoreGive(mutex_); }
        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;

      private:
        SemaphoreHandle_t mutex_;
    };

    // A client on a tag's list, and whether the latest state push to it was lost (resendDirty).
    struct Subscriber {
        int cid;
        bool dirty;
    };

    // Under encode_mutex_: the shared message and encode buffer, which a message that outgrows it leaves for the heap.
    // To one client as a reply, which may wait for it, or as a stream's frame, which does not; to every subscriber
    // when clientId is negative.
    template <typename T>
    bool encodeAndSend(const T& data, int clientId, bool asStream) {
        constexpr pb_size_t tag = MessageTraits<T>::tag;
        msg_.which_message = tag;
        MessageTraits<T>::assign(msg_, data);

        size_t out_size = 0;
        if (!pb_get_encoded_size(&out_size, socket_message_Message_fields, &msg_)) {
            ESP_LOGE("ProtoComm", "Failed to size message (tag %d)", (int)tag);
            return false;
        }
        uint8_t* buffer = pb_heap_enc_buf;
        if (out_size > sizeof(pb_heap_enc_buf)) {
            buffer = (uint8_t*)malloc(out_size);
            if (!buffer) {
                ESP_LOGE("ProtoComm", "No memory to encode message (tag %d, %u bytes)", (int)tag, (unsigned)out_size);
                return false;
            }
        }

        bool sent = false;
        pb_ostream_t stream = pb_ostream_from_buffer(buffer, out_size);
        if (!pb_encode(&stream, socket_message_Message_fields, &msg_)) {
            ESP_LOGE("ProtoComm", "Failed to encode message (tag %d): %s", (int)tag, PB_GET_ERROR(&stream));
        } else if (clientId < 0) {
            sent = sendToSubscribers(tag, buffer, stream.bytes_written);
        } else if (asStream) {
            sent = sendStream(buffer, stream.bytes_written, clientId);
        } else {
            sent = send(buffer, stream.bytes_written, clientId);
        }

        if (buffer != pb_heap_enc_buf) free(buffer);
        return sent;
    }

    // Under encode_mutex_: msg_ as set, a pong or an ack, which fits a few bytes on the stack.
    void sendSmall(int cid, bool asStream) {
        uint8_t bytes[16];
        pb_ostream_t stream = pb_ostream_from_buffer(bytes, sizeof(bytes));
        if (!pb_encode(&stream, socket_message_Message_fields, &msg_)) return;
        if (asStream)
            sendStream(bytes, stream.bytes_written, cid);
        else
            send(bytes, stream.bytes_written, cid);
    }

    const Topic* topicOf(int32_t tag) const {
        for (size_t i = 0; i < topicCount_; i++)
            if (topics_[i].tag == tag) return &topics_[i];
        return nullptr;
    }

    static Subscriber* findClient(std::list<Subscriber>& clients, int cid) {
        for (Subscriber& client : clients)
            if (client.cid == cid) return &client;
        return nullptr;
    }

    // Sends to a copy of the list, so a slow client never holds up subscribing or closing on the link's task.
    // Subscriptions stream the latest value, so a client that cannot take one now misses it: queueing it instead held
    // memory for every frame a slow link had not yet sent, until the heap ran out. A state push a client misses marks
    // it dirty for resendDirty, and one it takes clears the mark; a stream's frame is just lost.
    bool sendToSubscribers(int32_t tag, const uint8_t* data, size_t len) {
        std::vector<Subscriber> clients;
        {
            ScopedLock lock(mutex_);
            auto it = client_subscriptions_.find(tag);
            if (it == client_subscriptions_.end()) return true;
            clients.assign(it->second.begin(), it->second.end());
        }
        const Topic* topic = topicOf(tag);
        const bool state = topic && topic->kind == TopicKind::STATE;
        bool sent = true;
        for (const Subscriber& client : clients) {
            const bool delivered = sendStream(data, len, client.cid);
            if (state && delivered == client.dirty) markDirty(tag, client.cid, !delivered);
            sent = delivered && sent;
        }
        return sent;
    }

    void markDirty(int32_t tag, int cid, bool dirty) {
        ScopedLock lock(mutex_);
        auto it = client_subscriptions_.find(tag);
        if (it == client_subscriptions_.end()) return;
        if (Subscriber* client = findClient(it->second, cid)) client->dirty = dirty;
    }

    // Whether the client was marked; it no longer is.
    bool clearDirty(int32_t tag, int cid) {
        ScopedLock lock(mutex_);
        auto it = client_subscriptions_.find(tag);
        Subscriber* client = it == client_subscriptions_.end() ? nullptr : findClient(it->second, cid);
        if (!client || !client->dirty) return false;
        client->dirty = false;
        return true;
    }

    SemaphoreHandle_t mutex_;
    SemaphoreHandle_t encode_mutex_;
    std::map<int32_t, std::list<Subscriber>> client_subscriptions_;
    std::function<void(int32_t, int)> subscribedListener_;
    std::function<void(int)> heardListener_;
    std::function<void(int)> goneListener_;
    socket_message_Message msg_ = socket_message_Message_init_zero;
    uint8_t pb_heap_enc_buf[PROTO_BUFFER_SIZE];
    const Topic* topics_ = nullptr;
    size_t topicCount_ = 0;
};

template <typename T>
void SnapshotSink::put(const T& value) {
    delivered_ = link_.encodeAndSend(value, cid_, true);
}
