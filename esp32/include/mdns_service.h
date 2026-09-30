#pragma once

#include <mdns.h>
#include <functional>
#include <template/stateful_service.h>
#include <template/stateful_persistence.h>
#include <settings/mdns_settings.h>
#include <utils/timing.h>

class MDNSService : public StatefulService<MDNSSettings> {
  public:
    MDNSService();
    ~MDNSService();

    void begin();

    void status(api_MDNSStatus &status);
    /**
     * Browses for a service in a task of its own, since a query takes up to 3 s, and hands the
     * result to `done` from that task.
     */
    void queryAsync(const api_MDNSQueryRequest &request, std::function<void(const api_MDNSQueryResponse &)> done);

  private:
    FSPersistencePB<MDNSSettings> _persistence;
    bool _started {false};

    void reconfigureMDNS();
    void startMDNS();
    void stopMDNS();
    void addServices();
};
