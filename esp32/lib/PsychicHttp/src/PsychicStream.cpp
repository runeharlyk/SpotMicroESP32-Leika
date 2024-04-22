#include "PsychicStream.h"

/*****************************************/
// PsychicStream - Handler
/*****************************************/

PsychicStream::PsychicStream(const String &contentType)
    : PsychicHandler(), _onOpen(NULL), _onClose(NULL), _onRequest(NULL), _contentType(contentType)
{
}

PsychicStream::~PsychicStream()
{
}

PsychicStreamClient *PsychicStream::getClient(int socket)
{
    PsychicClient *client = PsychicHandler::getClient(socket);

    if (client == NULL)
        return NULL;

    return (PsychicStreamClient *)client->_friend;
}

PsychicStreamClient *PsychicStream::getClient(PsychicClient *client)
{
    return getClient(client->socket());
}

esp_err_t PsychicStream::handleRequest(PsychicRequest *request)
{

    esp_err_t err = httpd_resp_set_type(request->request(), _contentType.c_str());

    for (HTTPHeader header : DefaultHeaders::Instance().getHeaders())
        httpd_resp_set_hdr(request->request(), header.field, header.value);

    // lookup our client
    PsychicClient *client = checkForNewClient(request->client());
    if (client->isNew)
    {
        // let our handler know.
        openCallback(client);
        // _onRequest(request->request());
    }

    // PsychicStreamResponse response = PsychicStreamResponse(request, _contentType);
    // esp_err_t err = ESP_OK; // response.beginSend();

    return ESP_OK;
}

PsychicStream *PsychicStream::onOpen(PsychicStreamClientCallback fn)
{
    _onOpen = fn;
    return this;
}

PsychicStream *PsychicStream::onClose(PsychicStreamClientCallback fn)
{
    _onClose = fn;
    return this;
}

PsychicStream *PsychicStream::onRequest(PsychicStreamRequestCallback fn)
{
    _onRequest = fn;
    return this;
}

void PsychicStream::addClient(PsychicClient *client)
{
    client->_friend = new PsychicStreamClient(client);
    PsychicHandler::addClient(client);
}

void PsychicStream::removeClient(PsychicClient *client)
{
    PsychicHandler::removeClient(client);
    delete (PsychicStreamResponse *)client->_friend;
    client->_friend = NULL;
}

void PsychicStream::openCallback(PsychicClient *client)
{
    PsychicStreamClient *buddy = getClient(client);
    if (buddy == NULL)
    {
        TRACE();
        return;
    }

    if (_onOpen != NULL)
        _onOpen(buddy);
}

void PsychicStream::closeCallback(PsychicClient *client)
{
    PsychicStreamClient *buddy = getClient(client);
    if (buddy == NULL)
    {
        TRACE();
        return;
    }

    if (_onClose != NULL)
        _onClose(getClient(buddy));
}

void PsychicStream::write(const uint8_t *buffer, size_t size)
{
    if (_clients.size() == 0)
        return;
    ESP_LOGI("PsychicStream", "Writing to %d clients", _clients.size());
    for (PsychicClient *c : _clients)
    {
        ((PsychicStreamClient *)c->_friend)->write(buffer, size);
    }
}

/*****************************************/
// PsychicStreamClient
/*****************************************/

PsychicStreamClient::PsychicStreamClient(PsychicClient *client) : PsychicClient(client->server(), client->socket())
{
}

PsychicStreamClient::~PsychicStreamClient()
{
}

void PsychicStreamClient::write(const uint8_t *buffer, size_t size)
{
    int result;
    do
    {
        result = httpd_socket_send(this->server(), this->socket(), (const char *)buffer, size, 0);
    } while (result == HTTPD_SOCK_ERR_TIMEOUT);
}
