#include <filesystem_ws.h>
#include <filesystem.h>
#include <pb_encode.h>
#include <pb_decode.h>

static const char* TAG = "FileSystemWS";

namespace FileSystemWS {

FileSystemHandler fsHandler;

FileSystemHandler::FileSystemHandler() : transferIdCounter_(0) {}

std::string FileSystemHandler::generateTransferId() {
    return "xfer_" + std::to_string(millis()) + "_" + std::to_string(++transferIdCounter_);
}

void FileSystemHandler::cleanupExpiredTransfers() {
    uint32_t now = millis();
    auto it = transfers_.begin();
    while (it != transfers_.end()) {
        if (now - it->second.lastActivityTime > FS_TRANSFER_TIMEOUT) {
            if (it->second.file) {
                it->second.file.close();
            }
            ESP_LOGW(TAG, "Transfer %s timed out", it->first.c_str());
            it = transfers_.erase(it);
        } else {
            ++it;
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

    while (file && fileCount < 20 && dirCount < 20) {  // Limit to match protobuf max_count
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

socket_message_FSDownloadStartResponse FileSystemHandler::handleDownloadStart(const socket_message_FSDownloadStartRequest& req) {
    socket_message_FSDownloadStartResponse response = socket_message_FSDownloadStartResponse_init_zero;

    std::string path(req.path);
    ESP_LOGI(TAG, "Download start request: %s", path.c_str());

    if (!ESP_FS.exists(path.c_str())) {
        response.success = false;
        strncpy(response.error, "File not found", sizeof(response.error) - 1);
        return response;
    }

    File file = ESP_FS.open(path.c_str(), "r");
    if (!file || file.isDirectory()) {
        response.success = false;
        strncpy(response.error, "Cannot open file for reading", sizeof(response.error) - 1);
        return response;
    }

    uint32_t fileSize = file.size();
    uint32_t chunkSize = FS_MAX_CHUNK_SIZE;
    uint32_t totalChunks = (fileSize + chunkSize - 1) / chunkSize;

    std::string transferId = generateTransferId();

    TransferState state;
    state.path = path;
    state.file = file;
    state.fileSize = fileSize;
    state.chunkSize = chunkSize;
    state.totalChunks = totalChunks;
    state.chunksProcessed = 0;
    state.lastActivityTime = millis();
    state.isUpload = false;

    transfers_[transferId] = state;

    response.success = true;
    response.file_size = fileSize;
    response.chunk_size = chunkSize;
    response.total_chunks = totalChunks;
    strncpy(response.transfer_id, transferId.c_str(), sizeof(response.transfer_id) - 1);

    ESP_LOGI(TAG, "Download started: %s, size=%u, chunks=%u, id=%s", path.c_str(), fileSize, totalChunks, transferId.c_str());

    return response;
}

socket_message_FSDownloadChunkResponse FileSystemHandler::handleDownloadChunk(const socket_message_FSDownloadChunkRequest& req) {
    socket_message_FSDownloadChunkResponse response = socket_message_FSDownloadChunkResponse_init_zero;

    std::string transferId(req.transfer_id);
    strncpy(response.transfer_id, transferId.c_str(), sizeof(response.transfer_id) - 1);
    response.chunk_index = req.chunk_index;

    auto it = transfers_.find(transferId);
    if (it == transfers_.end()) {
        strncpy(response.error, "Invalid transfer ID", sizeof(response.error) - 1);
        return response;
    }

    TransferState& state = it->second;
    state.lastActivityTime = millis();

    // Seek to chunk position
    uint32_t position = req.chunk_index * state.chunkSize;
    if (!state.file.seek(position)) {
        strncpy(response.error, "Failed to seek file", sizeof(response.error) - 1);
        return response;
    }

    // Calculate chunk size (last chunk might be smaller)
    uint32_t bytesToRead = state.chunkSize;
    if (req.chunk_index == state.totalChunks - 1) {
        bytesToRead = state.fileSize - position;
    }

    // Read chunk data
    size_t bytesRead = state.file.read(response.data.bytes, bytesToRead);
    response.data.size = bytesRead;

    response.is_last = (req.chunk_index == state.totalChunks - 1);

    ESP_LOGI(TAG, "Download chunk %u/%u: %u bytes", req.chunk_index + 1, state.totalChunks, bytesRead);

    // Cleanup if last chunk
    if (response.is_last) {
        state.file.close();
        transfers_.erase(it);
        ESP_LOGI(TAG, "Download completed: %s", transferId.c_str());
    }

    return response;
}

socket_message_FSUploadStartResponse FileSystemHandler::handleUploadStart(const socket_message_FSUploadStartRequest& req) {
    socket_message_FSUploadStartResponse response = socket_message_FSUploadStartResponse_init_zero;

    std::string path(req.path);
    ESP_LOGI(TAG, "Upload start request: %s, size=%u", path.c_str(), req.file_size);

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

    std::string transferId = generateTransferId();

    TransferState state;
    state.path = path;
    state.file = file;
    state.fileSize = req.file_size;
    state.chunkSize = req.chunk_size > FS_MAX_CHUNK_SIZE ? FS_MAX_CHUNK_SIZE : req.chunk_size;
    state.totalChunks = (req.file_size + state.chunkSize - 1) / state.chunkSize;
    state.chunksProcessed = 0;
    state.lastActivityTime = millis();
    state.isUpload = true;

    transfers_[transferId] = state;

    response.success = true;
    response.max_chunk_size = FS_MAX_CHUNK_SIZE;
    strncpy(response.transfer_id, transferId.c_str(), sizeof(response.transfer_id) - 1);

    ESP_LOGI(TAG, "Upload started: %s, id=%s", path.c_str(), transferId.c_str());

    return response;
}

socket_message_FSUploadChunkResponse FileSystemHandler::handleUploadChunk(const socket_message_FSUploadChunkRequest& req) {
    socket_message_FSUploadChunkResponse fs_up_response = socket_message_FSUploadChunkResponse_init_zero;

    std::string transferId(req.transfer_id);
    strncpy(fs_up_response.transfer_id, transferId.c_str(), sizeof(fs_up_response.transfer_id) - 1);
    fs_up_response.chunk_index = req.chunk_index;

    auto it = transfers_.find(transferId);
    if (it == transfers_.end()) {
        fs_up_response.success = false;
        strncpy(fs_up_response.error, "Invalid transfer ID", sizeof(fs_up_response.error) - 1);
        return fs_up_response;
    }

    TransferState& state = it->second;
    state.lastActivityTime = millis();

    // Write chunk data
    size_t bytesWritten = state.file.write(req.data.bytes, req.data.size);
    if (bytesWritten != req.data.size) {
        fs_up_response.success = false;
        strncpy(fs_up_response.error, "Failed to write chunk", sizeof(fs_up_response.error) - 1);
        state.file.close();
        transfers_.erase(it);
        return fs_up_response;
    }

    // Flush periodically to prevent LittleFS buffer issues (every 64 chunks = ~64KB with 1KB chunks)
    if (state.chunksProcessed > 0 && state.chunksProcessed % 64 == 0) {
        ESP_LOGI(TAG, "Flushing file at chunk %u", state.chunksProcessed);
        state.file.flush();
    }

    state.chunksProcessed++;
    fs_up_response.success = true;
    fs_up_response.transfer_complete = req.is_last;

    ESP_LOGI(TAG, "Upload chunk %u/%u: %u bytes", state.chunksProcessed, state.totalChunks, bytesWritten);

    // Cleanup if last chunk
    if (req.is_last) {
        state.file.close();
        transfers_.erase(it);
        ESP_LOGI(TAG, "Upload completed: %s", state.path.c_str());
    }

    return fs_up_response;
}

socket_message_FSCancelTransferResponse FileSystemHandler::handleCancelTransfer(const socket_message_FSCancelTransferRequest& req) {
    socket_message_FSCancelTransferResponse response = socket_message_FSCancelTransferResponse_init_zero;

    std::string transferId(req.transfer_id);
    auto it = transfers_.find(transferId);

    if (it == transfers_.end()) {
        response.success = false;
        return response;
    }

    if (it->second.file) {
        it->second.file.close();
    }

    // Delete partial upload file
    if (it->second.isUpload) {
        ESP_FS.remove(it->second.path.c_str());
    }

    transfers_.erase(it);
    response.success = true;

    ESP_LOGI(TAG, "Transfer cancelled: %s", transferId.c_str());

    return response;
}

} // namespace FileSystemWS
