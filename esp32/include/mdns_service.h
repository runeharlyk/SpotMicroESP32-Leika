#pragma once

#include <mdns.h>
#include <functional>
#include <string>
#include <platform_shared/api.pb.h>

/**
 * The robot on mDNS: <hostname>.local, and the services it offers, among them `_spotmicro._tcp`,
 * whose TXT records (id, variant, version) let one robot find the others. The hostname is the WiFi
 * hostname and the instance name the robot's name; both follow their settings.
 */
class MDNSService {
  public:
    ~MDNSService();

    /** Starts mDNS; call once WiFi is set up. */
    void begin(const char *hostname, const char *instance);
    void setHostname(const char *hostname);
    void setInstance(const char *instance);

    void status(api_MDNSStatus &status);
    /**
     * Browses for a service in a task of its own, since a query takes up to 3 s, and hands the
     * result to `done` from that task.
     */
    void queryAsync(const api_MDNSQueryRequest &request, std::function<void(const api_MDNSQueryResponse &)> done);

  private:
    bool _started {false};
    std::string _hostname;
    std::string _instance;

    void advertise();
};
