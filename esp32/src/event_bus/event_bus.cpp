#include <event_bus/event_bus.h>
#include <vector>
#include <memory>
#include <mutex>
#include <algorithm>

std::atomic<bool> EventBus::_hasGlobalListeners {false};

struct GlobalSubscription {
    size_t id;
    std::unique_ptr<EventBus::GlobalHandler> handler;
};

static std::vector<GlobalSubscription> globalSubs;
static std::mutex globalMutex;
static size_t nextGlobalId = 1;

size_t EventBus::subscribeGlobal(GlobalHandler&& handler) {
    std::lock_guard<std::mutex> lock(globalMutex);
    size_t id = nextGlobalId++;
    auto h = std::make_unique<GlobalHandler>(std::move(handler));
    globalSubs.push_back({id, std::move(h)});
    _hasGlobalListeners.store(true, std::memory_order_release);
    return id;
}

void EventBus::unsubscribeGlobal(size_t id) {
    std::lock_guard<std::mutex> lock(globalMutex);
    globalSubs.erase(std::remove_if(globalSubs.begin(), globalSubs.end(), [id](const auto& s) { return s.id == id; }),
                     globalSubs.end());
    _hasGlobalListeners.store(!globalSubs.empty(), std::memory_order_release);
}

template <typename Msg>
void EventBus::notifyGlobalListeners(const Msg& msg, const char* source) {
    uint64_t timestamp = esp_timer_get_time();
    EventType type = EventTypeTraits<Msg>::type;

    std::lock_guard<std::mutex> lock(globalMutex);
    for (auto& sub : globalSubs) {
        (*sub.handler)(type, &msg, sizeof(Msg), timestamp);
    }
}

template void EventBus::notifyGlobalListeners<api_WifiSettings>(const api_WifiSettings&, const char*);
template void EventBus::notifyGlobalListeners<api_APSettings>(const api_APSettings&, const char*);
template void EventBus::notifyGlobalListeners<api_MDNSSettings>(const api_MDNSSettings&, const char*);
template void EventBus::notifyGlobalListeners<api_PeripheralSettings>(const api_PeripheralSettings&, const char*);
template void EventBus::notifyGlobalListeners<api_ServoSettings>(const api_ServoSettings&, const char*);
template void EventBus::notifyGlobalListeners<api_CameraSettings>(const api_CameraSettings&, const char*);
template void EventBus::notifyGlobalListeners<socket_message_IMUData>(const socket_message_IMUData&, const char*);
template void EventBus::notifyGlobalListeners<socket_message_ControllerData>(const socket_message_ControllerData&,
                                                                             const char*);
template void EventBus::notifyGlobalListeners<socket_message_ModeData>(const socket_message_ModeData&, const char*);
template void EventBus::notifyGlobalListeners<socket_message_AnglesData>(const socket_message_AnglesData&, const char*);
template void EventBus::notifyGlobalListeners<socket_message_WalkGaitData>(const socket_message_WalkGaitData&,
                                                                           const char*);
template void EventBus::notifyGlobalListeners<socket_message_ServoStateData>(const socket_message_ServoStateData&,
                                                                             const char*);
template void EventBus::notifyGlobalListeners<socket_message_ServoPWMData>(const socket_message_ServoPWMData&,
                                                                           const char*);
