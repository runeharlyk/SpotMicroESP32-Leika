#include <filesystem_ws.h>
#include <filesystem.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <esp_littlefs.h>

static const char* TAG = "FileSystemWS";

namespace FileSystemWS {

FileSystemHandler fsHandler;

FileSystemHandler::FileSystemHandler() : transferIdCounter_(0) {}

void FileSystemHandler::setSendCallbacks(
    SendMetadataCallback sendMetadata,
    SendCallback sendData,
    SendCompleteCallback sendComplete,
    SendUploadCompleteCallback sendUploadComplete
) {
    sendMetadataCallback_ = sendMetadata;
    sendDataCallback_ = sendData;
    sendCompleteCallback_ = sendComplete;
    sendUploadCompleteCallback_ = sendUploadComplete;
}

void FileSystemHandler::cleanupExpiredTransfers() {
    uint32_t now = millis();

    auto dlIt = downloads_.begin();
    while (dlIt != downloads_.end()) {
        if (now - dlIt->second.lastActivityTime > FS_TRANSFER_TIMEOUT) {
            if (dlIt->second.file) {
                dlIt->second.file.close();
            }
            ESP_LOGW(TAG, "Download %u timed out", dlIt->first);

            if (sendCompleteCallback_) {
                socket_message_FSDownloadComplete complete = socket_message_FSDownloadComplete_init_zero;
                complete.transfer_id = dlIt->first;
                complete.success = false;
                strncpy(complete.error, "Transfer timed out", sizeof(complete.error) - 1);
                complete.total_chunks = dlIt->second.chunksSent;
                complete.file_size = dlIt->second.fileSize;
                sendCompleteCallback_(complete, dlIt->second.clientId);
            }

            dlIt = downloads_.erase(dlIt);
        } else {
            ++dlIt;
        }
    }

    auto ulIt = uploads_.begin();
    while (ulIt != uploads_.end()) {
        if (now - ulIt->second.lastActivityTime > FS_TRANSFER_TIMEOUT) {
            if (ulIt->second.file) {
                ulIt->second.file.close();
            }
            ESP_FS.remove(ulIt->second.path.c_str());
            ESP_LOGW(TAG, "Upload %u timed out, deleted partial file", ulIt->first);

            if (sendUploadCompleteCallback_) {
                socket_message_FSUploadComplete complete = socket_message_FSUploadComplete_init_zero;
                complete.transfer_id = ulIt->first;
                complete.success = false;
                strncpy(complete.error, "Transfer timed out", sizeof(complete.error) - 1);
                complete.chunks_received = ulIt->second.chunksReceived;
                sendUploadCompleteCallback_(complete, ulIt->second.clientId);
            }

            ulIt = uploads_.erase(ulIt);
        } else {
            ++ulIt;
        }
    }
}

socket_message_FSDeleteResponse FileSystemHandler::handleDelete(const socket_message_FSDeleteRequest& req) {
    socket_message_FSDeleteResponse response = socket_message_FSDeleteResponse_init_zero;

    std::string path(req.path);
    ESP_LOGI(TAG, "Delete request: %s", path.c_str());

    if (!ESP_FS.exists(path.c_str())) {
        response.success = false;
        strncpy(response.error, "File or directory not found", sizeof(response.error) - 1);
        return response;
    }

    if (deleteRecursive(path)) {
        response.success = true;
    } else {
        response.success = false;
        strncpy(response.error, "Failed to delete", sizeof(response.error) - 1);
    }

    return response;
}

bool FileSystemHandler::deleteRecursive(const std::string& path) {
    File file = ESP_FS.open(path.c_str());
    if (!file) return false;

    if (file.isDirectory()) {
        File child = file.openNextFile();
        while (child) {
            std::string childPath = std::string(child.name());
            child.close();
            if (!deleteRecursive(childPath)) {
                file.close();
                return false;
            }
            child = file.openNextFile();
        }
        file.close();
        return ESP_FS.rmdir(path.c_str());
    } else {
        file.close();
        return ESP_FS.remove(path.c_str());
    }
}

socket_message_FSMkdirResponse FileSystemHandler::handleMkdir(const socket_message_FSMkdirRequest& req) {
    socket_message_FSMkdirResponse response = socket_message_FSMkdirResponse_init_zero;

    std::string path(req.path);
    ESP_LOGI(TAG, "Mkdir request: %s", path.c_str());

    if (ESP_FS.exists(path.c_str())) {
        response.success = false;
        strncpy(response.error, "Path already exists", sizeof(response.error) - 1);
        return response;
    }

    if (ESP_FS.mkdir(path.c_str())) {
        response.success = true;
    } else {
        response.success = false;
        strncpy(response.error, "Failed to create directory", sizeof(response.error) - 1);
    }

    return response;
}

void FileSystemHandler::listDirectory(const std::string& path, socket_message_FSListResponse& response) {
    File dir = ESP_FS.open(path.c_str());
    if (!dir || !dir.isDirectory()) {
        return;
    }

    File file = dir.openNextFile();
    int fileCount = 0;
    int dirCount = 0;

    while (file && fileCount < 20 && dirCount < 20) { // Limit to match protobuf max_count
        if (file.isDirectory()) {
            if (dirCount < 20) {
                strncpy(response.directories[dirCount].name, file.name(), sizeof(response.directories[dirCount].name) - 1);
                dirCount++;
            }
        } else {
            if (fileCount < 20) {
                strncpy(response.files[fileCount].name, file.name(), sizeof(response.files[fileCount].name) - 1);
                response.files[fileCount].size = file.size();
                fileCount++;
            }
        }
        file = dir.openNextFile();
    }

    response.files_count = fileCount;
    response.directories_count = dirCount;
}

socket_message_FSListResponse FileSystemHandler::handleList(const socket_message_FSListRequest& req) {
    socket_message_FSListResponse response = socket_message_FSListResponse_init_zero;

    std::string path(req.path);
    if (path.empty()) path = "/";

    ESP_LOGI(TAG, "List request: %s", path.c_str());

    if (!ESP_FS.exists(path.c_str())) {
        response.success = false;
        strncpy(response.error, "Path not found", sizeof(response.error) - 1);
        return response;
    }

    listDirectory(path, response);
    response.success = true;

    return response;
}

// ===== STREAMING DOWNLOAD =====

void FileSystemHandler::handleDownloadRequest(const socket_message_FSDownloadRequest& req, int clientId) {
    std::string path(req.path);
    ESP_LOGI(TAG, "Download request: %s", path.c_str());

    // Validate file exists
    if (!ESP_FS.exists(path.c_str())) {
        if (sendMetadataCallback_) {
            socket_message_FSDownloadMetadata metadata = socket_message_FSDownloadMetadata_init_zero;
            metadata.success = false;
            strncpy(metadata.error, "File not found", sizeof(metadata.error) - 1);
            sendMetadataCallback_(metadata, clientId);
        }
        return;
    }

    File file = ESP_FS.open(path.c_str(), "r");
    if (!file || file.isDirectory()) {
        if (sendMetadataCallback_) {
            socket_message_FSDownloadMetadata metadata = socket_message_FSDownloadMetadata_init_zero;
            metadata.success = false;
            strncpy(metadata.error, "Cannot open file for reading", sizeof(metadata.error) - 1);
            sendMetadataCallback_(metadata, clientId);
        }
        return;
    }

    // Set file buffer size large, so we use ram to read from
    // Set buffer size so 1 mb (static) TODO: Check that there is enough ram to do this
    file.setBufferSize(1000000);

    uint32_t fileSize = file.size();
    uint32_t chunkSize = FS_MAX_CHUNK_SIZE;
    uint32_t totalChunks = (fileSize + chunkSize - 1) / chunkSize;
    if (totalChunks == 0) totalChunks = 1;

    uint32_t transferId = generateTransferId();

    if (sendMetadataCallback_) {
        socket_message_FSDownloadMetadata metadata = socket_message_FSDownloadMetadata_init_zero;
        metadata.transfer_id = transferId;
        metadata.success = true;
        metadata.file_size = fileSize;
        metadata.total_chunks = totalChunks;
        sendMetadataCallback_(metadata, clientId);
    }

    DownloadState state;
    state.path = path;
    state.file = file;
    state.fileSize = fileSize;
    state.chunkSize = chunkSize;
    state.totalChunks = totalChunks;
    state.chunksSent = 0;
    state.lastActivityTime = millis();
    state.clientId = clientId;

    downloads_[transferId] = state;

    ESP_LOGI(TAG, "Download started: %s, size=%u, chunks=%u, id=%u",
             path.c_str(), fileSize, totalChunks, transferId);

    // Start streaming chunks immediately
    while (sendNextDownloadChunk(transferId)) {
        // Keep sending until done or error
        taskYIELD(); // Allow other tasks to run
    }
}

bool FileSystemHandler::sendNextDownloadChunk(uint32_t transferId) {
    auto it = downloads_.find(transferId);
    if (it == downloads_.end()) {
        return false;
    }

    DownloadState& state = it->second;
    state.lastActivityTime = millis();

    if (state.chunksSent >= state.totalChunks) {
        if (sendCompleteCallback_) {
            socket_message_FSDownloadComplete complete = socket_message_FSDownloadComplete_init_zero;
            complete.transfer_id = transferId;
            complete.success = true;
            complete.total_chunks = state.totalChunks;
            complete.file_size = state.fileSize;
            sendCompleteCallback_(complete, state.clientId);
        }

        state.file.close();
        downloads_.erase(it);
        ESP_LOGI(TAG, "Download completed: %u", transferId);
        return false;
    }

    auto data = new socket_message_FSDownloadData();
    memset(data, 0, sizeof(socket_message_FSDownloadData));
    data->transfer_id = transferId;
    data->chunk_index = state.chunksSent;

    uint32_t bytesToRead = state.chunkSize;
    uint32_t position = state.chunksSent * state.chunkSize;
    if (position + bytesToRead > state.fileSize) {
        bytesToRead = state.fileSize - position;
    }

    size_t bytesRead = state.file.read(data->data.bytes, bytesToRead);
    if (bytesRead == 0 && bytesToRead > 0) {
        delete data;
        if (sendCompleteCallback_) {
            socket_message_FSDownloadComplete complete = socket_message_FSDownloadComplete_init_zero;
            complete.transfer_id = transferId;
            complete.success = false;
            strncpy(complete.error, "Failed to read file", sizeof(complete.error) - 1);
            complete.total_chunks = state.chunksSent;
            complete.file_size = state.fileSize;
            sendCompleteCallback_(complete, state.clientId);
        }

        state.file.close();
        downloads_.erase(it);
        ESP_LOGE(TAG, "Download failed - read error: %u", transferId);
        return false;
    }
    data->data.size = bytesRead;

    if (sendDataCallback_) {
        sendDataCallback_(*data, state.clientId);
    }

    delete data;
    state.chunksSent++;
    ESP_LOGD(TAG, "Download chunk %u/%u sent: %u bytes", state.chunksSent, state.totalChunks, bytesRead);

    return true;
}

// ===== STREAMING UPLOAD =====

socket_message_FSUploadStartResponse FileSystemHandler::handleUploadStart(const socket_message_FSUploadStart& req,
                                                                          int clientId) {
    socket_message_FSUploadStartResponse response = socket_message_FSUploadStartResponse_init_zero;

    std::string path(req.path);
    ESP_LOGI(TAG, "Upload start request: %s, size=%u, chunks=%u", path.c_str(), req.file_size, req.total_chunks);

    // Check available space
    size_t fs_total = 0, fs_used = 0;
    esp_littlefs_info("spiffs", &fs_total, &fs_used);
    size_t freeSpace = fs_total - fs_used;
    if (freeSpace < req.file_size + 4096) { // 4KB safety margin
        response.success = false;
        strncpy(response.error, "Insufficient storage space", sizeof(response.error) - 1);
        return response;
    }

    // Ensure parent directory exists
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash != std::string::npos && lastSlash > 0) {
        std::string parentDir = path.substr(0, lastSlash);
        if (!ESP_FS.exists(parentDir.c_str())) {
            response.success = false;
            strncpy(response.error, "Parent directory does not exist", sizeof(response.error) - 1);
            return response;
        }
    }

    File file = ESP_FS.open(path.c_str(), FILE_WRITE);
    if (!file) {
        response.success = false;
        strncpy(response.error, "Cannot open file for writing", sizeof(response.error) - 1);
        return response;
    }

    file.setBufferSize(1000000);

    uint32_t transferId = generateTransferId();

    UploadState state;
    state.path = path;
    state.file = file;
    state.fileSize = req.file_size;
    state.totalChunks = req.total_chunks;
    state.chunksReceived = 0;
    state.bytesReceived = 0;
    state.lastActivityTime = millis();
    state.clientId = clientId;
    state.hasError = false;

    uploads_[transferId] = state;

    response.success = true;
    response.transfer_id = transferId;

    ESP_LOGI(TAG, "Upload started: %s, id=%u", path.c_str(), transferId);

    return response;
}

void FileSystemHandler::handleUploadData(const socket_message_FSUploadData& req) {
    uint32_t transferId = req.transfer_id;

    auto it = uploads_.find(transferId);
    if (it == uploads_.end()) {
        ESP_LOGW(TAG, "Upload data for unknown transfer: %u", transferId);
        return;
    }

    UploadState& state = it->second;
    state.lastActivityTime = millis();

    // Skip if we already have an error
    if (state.hasError) {
        return;
    }

    // Validate chunk index (allow out-of-order but warn)
    if (req.chunk_index != state.chunksReceived) {
        ESP_LOGW(TAG, "Upload chunk out of order: expected %u, got %u", state.chunksReceived, req.chunk_index);
        // For now, we'll accept it anyway and write sequentially
        // A more robust implementation would buffer out-of-order chunks
    }

    // Write chunk data
    size_t bytesWritten = state.file.write(req.data.bytes, req.data.size);
    if (bytesWritten != req.data.size) {
        state.hasError = true;
        state.errorMessage = "Failed to write chunk";
        finalizeUpload(transferId, false, state.errorMessage);
        return;
    }

    state.chunksReceived++;
    state.bytesReceived += bytesWritten;

    // Flush periodically to prevent LittleFS buffer issues
    if (state.chunksReceived > 0 && state.chunksReceived % 64 == 0) {
        ESP_LOGD(TAG, "Flushing file at chunk %u", state.chunksReceived);
        state.file.flush();
    }

    ESP_LOGD(TAG, "Upload chunk %u/%u: %u bytes", state.chunksReceived, state.totalChunks, bytesWritten);

    // Check if upload is complete
    if (state.chunksReceived >= state.totalChunks) {
        finalizeUpload(transferId, true);
    }
}

void FileSystemHandler::finalizeUpload(uint32_t transferId, bool success, const std::string& error) {
    auto it = uploads_.find(transferId);
    if (it == uploads_.end()) {
        return;
    }

    UploadState& state = it->second;

    if (state.file) {
        state.file.close();
    }

    if (!success) {
        ESP_FS.remove(state.path.c_str());
        ESP_LOGW(TAG, "Upload failed, deleted partial file: %s", state.path.c_str());
    } else {
        ESP_LOGI(TAG, "Upload completed: %s (%u bytes)", state.path.c_str(), state.bytesReceived);
    }

    if (sendUploadCompleteCallback_) {
        socket_message_FSUploadComplete complete = socket_message_FSUploadComplete_init_zero;
        complete.transfer_id = transferId;
        complete.success = success;
        if (!error.empty()) {
            strncpy(complete.error, error.c_str(), sizeof(complete.error) - 1);
        }
        complete.chunks_received = state.chunksReceived;
        sendUploadCompleteCallback_(complete, state.clientId);
    }

    uploads_.erase(it);
}

// ===== TRANSFER CONTROL =====

socket_message_FSCancelTransferResponse FileSystemHandler::handleCancelTransfer(
    const socket_message_FSCancelTransfer& req) {
    socket_message_FSCancelTransferResponse response = socket_message_FSCancelTransferResponse_init_zero;
    uint32_t transferId = req.transfer_id;
    response.transfer_id = transferId;

    auto dlIt = downloads_.find(transferId);
    if (dlIt != downloads_.end()) {
        if (dlIt->second.file) {
            dlIt->second.file.close();
        }
        downloads_.erase(dlIt);
        response.success = true;
        ESP_LOGI(TAG, "Download cancelled: %u", transferId);
        return response;
    }

    auto ulIt = uploads_.find(transferId);
    if (ulIt != uploads_.end()) {
        if (ulIt->second.file) {
            ulIt->second.file.close();
        }
        ESP_FS.remove(ulIt->second.path.c_str());
        uploads_.erase(ulIt);
        response.success = true;
        ESP_LOGI(TAG, "Upload cancelled: %u", transferId);
        return response;
    }

    response.success = false;
    return response;
}

} // namespace FileSystemWS
