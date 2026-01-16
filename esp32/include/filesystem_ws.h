#pragma once

#include <LittleFS.h>
#include <platform_shared/message.pb.h>
#include <map>
#include <string>
#include <functional>

// Make sure that this aligns with socket_message.FSDownloadData.data max_size (and for upload)
#define FS_MAX_CHUNK_SIZE 16384 // ~= 16 kb
#define FS_TRANSFER_TIMEOUT 30000  // 30 seconds

namespace FileSystemWS {

struct DownloadState {
    std::string path;
    File file;
    uint32_t fileSize;
    uint32_t chunkSize;
    uint32_t totalChunks;
    uint32_t chunksSent;
    uint32_t lastActivityTime;
    int clientId;
};

struct UploadState {
    std::string path;
    File file;
    uint32_t fileSize;
    uint32_t totalChunks;
    uint32_t chunksReceived;
    uint32_t bytesReceived;
    uint32_t lastActivityTime;
    int clientId;
    bool hasError;
    std::string errorMessage;
};

// Callback type for sending messages to clients
using SendCallback = std::function<void(const socket_message_FSDownloadData&, int clientId)>;
using SendCompleteCallback = std::function<void(const socket_message_FSDownloadComplete&, int clientId)>;
using SendUploadCompleteCallback = std::function<void(const socket_message_FSUploadComplete&, int clientId)>;

class FileSystemHandler {
  public:
    FileSystemHandler();

    // Set callbacks for sending streaming data
    void setSendCallbacks(
        SendCallback sendData,
        SendCompleteCallback sendComplete,
        SendUploadCompleteCallback sendUploadComplete
    );

    // Delete file/directory
    socket_message_FSDeleteResponse handleDelete(const socket_message_FSDeleteRequest& req);

    // Create directory
    socket_message_FSMkdirResponse handleMkdir(const socket_message_FSMkdirRequest& req);

    // List directory
    socket_message_FSListResponse handleList(const socket_message_FSListRequest& req);

    // Streaming download - starts the download and streams all chunks
    void handleDownloadRequest(const socket_message_FSDownloadRequest& req, int clientId);

    // Streaming upload - start upload session
    socket_message_FSUploadStartResponse handleUploadStart(const socket_message_FSUploadStart& req, int clientId);

    // Streaming upload - receive chunk data (fire-and-forget from client)
    void handleUploadData(const socket_message_FSUploadData& req);

    // Cancel transfer
    socket_message_FSCancelTransferResponse handleCancelTransfer(const socket_message_FSCancelTransfer& req);

    // Cleanup expired transfers
    void cleanupExpiredTransfers();

    // Process pending downloads (call from main loop)
    void processPendingDownloads();

  private:
    std::map<std::string, DownloadState> downloads_;
    std::map<std::string, UploadState> uploads_;
    uint32_t transferIdCounter_;

    SendCallback sendDataCallback_;
    SendCompleteCallback sendCompleteCallback_;
    SendUploadCompleteCallback sendUploadCompleteCallback_;

    std::string generateTransferId();
    void listDirectory(const std::string& path, socket_message_FSListResponse& response);
    bool deleteRecursive(const std::string& path);

    // Send next chunk for a download
    bool sendNextDownloadChunk(const std::string& transferId);

    // Finalize upload and send completion message
    void finalizeUpload(const std::string& transferId, bool success, const std::string& error = "");
};

extern FileSystemHandler fsHandler;

} // namespace FileSystemWS
