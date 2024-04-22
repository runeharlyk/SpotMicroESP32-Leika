#ifndef PsychicStream_H_
#define PsychicStream_H_

#include "PsychicClient.h"
#include "PsychicCore.h"
#include "PsychicHandler.h"
#include "PsychicResponse.h"
#include "PsychicStreamResponse.h"

class PsychicStream;
class PsychicStreamResponse;
class PsychicStreamClient;
class PsychicResponse;

typedef std::function<void(PsychicStreamClient *client)> PsychicStreamClientCallback;
typedef std::function<void(httpd_req_t *request)> PsychicStreamRequestCallback;

class PsychicStreamClient : public PsychicClient
{
    friend PsychicStream;

  public:
    PsychicStreamClient(PsychicClient *client);
    ~PsychicStreamClient();

    void write(const uint8_t *buffer, size_t size);
};

class PsychicStream : public PsychicHandler
{
  private:
    PsychicStreamClientCallback _onOpen;
    PsychicStreamClientCallback _onClose;
    PsychicStreamRequestCallback _onRequest;
    String _contentType;

  public:
    PsychicStream(const String &contentType);
    ~PsychicStream();

    PsychicStreamClient *getClient(int socket) override;
    PsychicStreamClient *getClient(PsychicClient *client) override;
    void addClient(PsychicClient *client) override;
    void removeClient(PsychicClient *client) override;
    void openCallback(PsychicClient *client) override;
    void closeCallback(PsychicClient *client) override;

    PsychicStream *onOpen(PsychicStreamClientCallback fn);
    PsychicStream *onClose(PsychicStreamClientCallback fn);
    PsychicStream *onRequest(PsychicStreamRequestCallback fn);

    esp_err_t handleRequest(PsychicRequest *request) override final;

    void write(const uint8_t *buffer, size_t size);
};

#endif /* PsychicStream_H_ */