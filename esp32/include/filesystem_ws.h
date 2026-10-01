#pragma once

#include <platform_shared/message.pb.h>
#include <filesystem.h>
#include <map>
#include <string>
#include <functional>
#include <cstdio>

#define FS_MAX_CHUNK_SIZE 16384
#define FS_TRANSFER_TIMEOUT_MS 30000

namespace FileSystemWS {

struct DownloadState {
    std::string path;
    FILE* file;
    uint32_t fileSize;
    uint32_t chunkSize;
    uint32_t totalChunks;
    uint32_t chunksSent;
    uint32_t lastActivityTime;
    int clientId;
};

struct UploadState {
    std::string path;
    FILE* file;
    uint32_t fileSize;
    uint32_t totalChunks;
    uint32_t chunksReceived;
    uint32_t bytesReceived;
    uint32_t lastActivityTime;
    int clientId;
    bool hasError;
    std::string errorMessage;

    // The data goes here until all of it arrived, so an interrupted upload leaves the file it replaces intact.
    std::string partPath() const { return path + ".part"; }
};

using SendMetadataCallback = std::function<void(const socket_message_FSDownloadMetadata&, int clientId)>;
using SendCallback = std::function<void(const socket_message_FSDownloadData&, int clientId)>;
using SendCompleteCallback = std::function<void(const socket_message_FSDownloadComplete&, int clientId)>;
using SendUploadCompleteCallback = std::function<void(const socket_message_FSUploadComplete&, int clientId)>;
// Queues work to run later on the socket's task; false when it could not be queued.
using RunLater = std::function<bool(std::function<void()>)>;
// Waits up to `ms` for a client's socket to take a whole chunk without blocking; false when it still cannot.
using WaitWritable = std::function<bool(int clientId, uint32_t ms)>;

class FileSystemHandler {
  public:
    FileSystemHandler();

    void setSendCallbacks(SendMetadataCallback sendMetadata, SendCallback sendData, SendCompleteCallback sendComplete,
                          SendUploadCompleteCallback sendUploadComplete);
    void setScheduling(RunLater runLater, WaitWritable waitWritable) {
        runLater_ = std::move(runLater);
        waitWritable_ = std::move(waitWritable);
    }

    socket_message_FSDeleteResponse handleDelete(const socket_message_FSDeleteRequest& req);
    socket_message_FSMkdirResponse handleMkdir(const socket_message_FSMkdirRequest& req);
    socket_message_FSListResponse handleList(const socket_message_FSListRequest& req);
    void handleDownloadRequest(const socket_message_FSDownloadRequest& req, int clientId);
    socket_message_FSUploadStartResponse handleUploadStart(const socket_message_FSUploadStart& req, int clientId);
    void handleUploadData(const socket_message_FSUploadData& req);
    socket_message_FSCancelTransferResponse handleCancelTransfer(const socket_message_FSCancelTransfer& req);

    // Ends a departed client's transfers, so none streams on to whoever is given its socket next.
    void dropClient(int clientId);

  private:
    // Only the socket's task handles transfers, so it also sweeps out the abandoned ones, as each new one starts.
    void cleanupExpiredTransfers();

    std::map<uint32_t, DownloadState> downloads_;
    std::map<uint32_t, UploadState> uploads_;
    uint32_t transferIdCounter_;

    inline uint32_t generateTransferId() { return ++transferIdCounter_; }

    SendMetadataCallback sendMetadataCallback_;
    SendCallback sendDataCallback_;
    SendCompleteCallback sendCompleteCallback_;
    SendUploadCompleteCallback sendUploadCompleteCallback_;
    RunLater runLater_;
    WaitWritable waitWritable_;

    void listDirectory(const std::string& path, socket_message_FSListResponse& response);
    bool deleteRecursive(const std::string& path);
    bool sendNextDownloadChunk(uint32_t transferId);
    void pumpDownload(uint32_t transferId);
    void failDownload(uint32_t transferId, const char* error);
    void finalizeUpload(uint32_t transferId, bool success, const std::string& error = "");
};

extern FileSystemHandler fsHandler;

} // namespace FileSystemWS
