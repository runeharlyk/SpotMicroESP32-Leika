#pragma once

#include <LittleFS.h>
#include <platform_shared/message.pb.h>
#include <map>
#include <string>

#define FS_MAX_CHUNK_SIZE 1024
#define FS_TRANSFER_TIMEOUT 30000  // 30 seconds

namespace FileSystemWS {

struct TransferState {
    std::string path;
    File file;
    uint32_t fileSize;
    uint32_t chunkSize;
    uint32_t totalChunks;
    uint32_t chunksProcessed;
    uint32_t lastActivityTime;
    bool isUpload;
};

class FileSystemHandler {
  public:
    FileSystemHandler();

    // Delete file/directory
    socket_message_FSDeleteResponse handleDelete(const socket_message_FSDeleteRequest& req);

    // Create directory
    socket_message_FSMkdirResponse handleMkdir(const socket_message_FSMkdirRequest& req);

    // List directory
    socket_message_FSListResponse handleList(const socket_message_FSListRequest& req);

    // Download operations (ESP -> Client)
    socket_message_FSDownloadStartResponse handleDownloadStart(const socket_message_FSDownloadStartRequest& req);
    socket_message_FSDownloadChunkResponse handleDownloadChunk(const socket_message_FSDownloadChunkRequest& req);

    // Upload operations (Client -> ESP)
    socket_message_FSUploadStartResponse handleUploadStart(const socket_message_FSUploadStartRequest& req);
    socket_message_FSUploadChunkResponse handleUploadChunk(const socket_message_FSUploadChunkRequest& req);

    // Cancel transfer
    socket_message_FSCancelTransferResponse handleCancelTransfer(const socket_message_FSCancelTransferRequest& req);

    // Cleanup expired transfers
    void cleanupExpiredTransfers();

  private:
    std::map<std::string, TransferState> transfers_;
    uint32_t transferIdCounter_;

    std::string generateTransferId();
    void listDirectory(const std::string& path, socket_message_FSListResponse& response);
    bool deleteRecursive(const std::string& path);
};

extern FileSystemHandler fsHandler;

} // namespace FileSystemWS
