# Chunked Filesystem Transfer System

This system enables chunked file uploads and downloads between the client (web browser) and ESP32 over WebSocket, overcoming the 1KB per stream limitation.

## Architecture

### Protocol Messages (Protobuf)

The system uses Protocol Buffers for efficient binary messaging with the following operations:

1. **Delete**: Remove files or directories
2. **Mkdir**: Create directories
3. **List**: List files and directories
4. **Download**: Transfer files from ESP32 to client (chunked)
5. **Upload**: Transfer files from client to ESP32 (chunked)
6. **Cancel**: Cancel ongoing transfers

### Chunked Transfer Flow

#### Download (ESP32 → Client)

1. Client sends `FSDownloadStartRequest` with file path
2. ESP32 responds with `FSDownloadStartResponse` containing:
   - Transfer ID
   - File size
   - Chunk size (1024 bytes max)
   - Total chunks
3. Client requests chunks sequentially using `FSDownloadChunkRequest`
4. ESP32 sends each chunk via `FSDownloadChunkResponse`
5. Transfer completes when last chunk is received

#### Upload (Client → ESP32)

1. Client sends `FSUploadStartRequest` with destination path and file size
2. ESP32 responds with `FSUploadStartResponse` containing:
   - Transfer ID
   - Max chunk size (1024 bytes)
3. Client sends chunks sequentially using `FSUploadChunkRequest`
4. ESP32 responds with `FSUploadChunkResponse` after each chunk
5. Transfer completes when last chunk is written

## Implementation Details

### ESP32 Side

**Files:**
- `esp32/include/filesystem_ws.h` - Header file with handler class definition
- `esp32/src/filesystem_ws.cpp` - Implementation of filesystem operations
- `esp32/include/communication/proto_helpers.h` - Message traits for protobuf

**Key Features:**
- Transfer state management with automatic cleanup
- Timeout handling (30 seconds of inactivity)
- Recursive directory deletion
- File integrity verification

**Integration:**
You need to integrate the filesystem handlers into your WebSocket message handling. In your main WebSocket handler, add:

```cpp
#include <filesystem_ws.h>

// In your correlation request handler:
void handleCorrelationRequest(const socket_message_CorrelationRequest& request, int clientId) {
    socket_message_CorrelationResponse response;

    // ... existing handlers ...

    if (request.which_request == socket_message_CorrelationRequest_fs_delete_request_tag) {
        response.which_response = socket_message_CorrelationResponse_fs_delete_response_tag;
        response.response.fs_delete_response =
            FileSystemWS::fsHandler.handleDelete(request.request.fs_delete_request);
    }
    else if (request.which_request == socket_message_CorrelationRequest_fs_mkdir_request_tag) {
        response.which_response = socket_message_CorrelationResponse_fs_mkdir_response_tag;
        response.response.fs_mkdir_response =
            FileSystemWS::fsHandler.handleMkdir(request.request.fs_mkdir_request);
    }
    else if (request.which_request == socket_message_CorrelationRequest_fs_list_request_tag) {
        response.which_response = socket_message_CorrelationResponse_fs_list_response_tag;
        response.response.fs_list_response =
            FileSystemWS::fsHandler.handleList(request.request.fs_list_request);
    }
    else if (request.which_request == socket_message_CorrelationRequest_fs_download_start_request_tag) {
        response.which_response = socket_message_CorrelationResponse_fs_download_start_response_tag;
        response.response.fs_download_start_response =
            FileSystemWS::fsHandler.handleDownloadStart(request.request.fs_download_start_request);
    }
    else if (request.which_request == socket_message_CorrelationRequest_fs_download_chunk_request_tag) {
        response.which_response = socket_message_CorrelationResponse_fs_download_chunk_response_tag;
        response.response.fs_download_chunk_response =
            FileSystemWS::fsHandler.handleDownloadChunk(request.request.fs_download_chunk_request);
    }
    else if (request.which_request == socket_message_CorrelationRequest_fs_upload_start_request_tag) {
        response.which_response = socket_message_CorrelationResponse_fs_upload_start_response_tag;
        response.response.fs_upload_start_response =
            FileSystemWS::fsHandler.handleUploadStart(request.request.fs_upload_start_request);
    }
    else if (request.which_request == socket_message_CorrelationRequest_fs_upload_chunk_request_tag) {
        response.which_response = socket_message_CorrelationResponse_fs_upload_chunk_response_tag;
        response.response.fs_upload_chunk_response =
            FileSystemWS::fsHandler.handleUploadChunk(request.request.fs_upload_chunk_request);
    }
    else if (request.which_request == socket_message_CorrelationRequest_fs_cancel_transfer_request_tag) {
        response.which_response = socket_message_CorrelationResponse_fs_cancel_transfer_response_tag;
        response.response.fs_cancel_transfer_response =
            FileSystemWS::fsHandler.handleCancelTransfer(request.request.fs_cancel_transfer_request);
    }

    // Send response back to client
    sendCorrelationResponse(response, clientId);
}

// Optionally, in your main loop or timer:
void loop() {
    // Clean up expired transfers periodically
    FileSystemWS::fsHandler.cleanupExpiredTransfers();
}
```

### Client Side (TypeScript/Svelte)

**Files:**
- `app/src/lib/filesystem/chunkedTransfer.ts` - Client library for file transfers
- `app/src/lib/components/filesystem/FileManager.svelte` - Example UI component

**Usage Example:**

```typescript
import { fileSystemClient } from '$lib/filesystem/chunkedTransfer'

// Upload a file
const file = new File(['Hello World'], 'test.txt')
const result = await fileSystemClient.uploadFileFromBrowser('/test.txt', file, (progress) => {
    console.log(`Upload: ${progress.percentage}%`)
})

// Download a file
const download = await fileSystemClient.downloadFileAndSave(
    '/test.txt',
    'test.txt',
    (progress) => {
        console.log(`Download: ${progress.percentage}%`)
    }
)

// List directory
const listing = await fileSystemClient.listDirectory('/')
console.log('Files:', listing.files)
console.log('Directories:', listing.directories)

// Create directory
await fileSystemClient.createDirectory('/new_folder')

// Delete file
await fileSystemClient.deleteFile('/old_file.txt')
```

## Configuration

### Maximum Chunk Size

Currently set to 1024 bytes (`FS_MAX_CHUNK_SIZE`) to work within ESP32 WebSocket frame limitations. Adjust if your setup allows larger frames.

### Transfer Timeout

Transfers inactive for 30 seconds (`FS_TRANSFER_TIMEOUT`) are automatically cleaned up. Increase for slower connections.

## Error Handling

- Network errors: Transfers are automatically cancelled
- Timeouts: Inactive transfers are cleaned up on ESP32
- File errors: Detailed error messages returned to client
- Partial uploads: Cancelled uploads delete the partial file on ESP32

## Performance Considerations

- **Sequential Chunks**: Chunks are sent sequentially to ensure order and reliability
- **Memory Usage**: ESP32 keeps one File handle open per active transfer
- **Browser Memory**: Downloads buffer entire file in memory before saving
- **Network**: ~1KB per message overhead due to protobuf encoding

## Security Notes

- No authentication/authorization implemented - add as needed
- Path traversal: Validate paths to prevent access outside allowed directories
- File size limits: Consider adding max file size restrictions
- Rate limiting: Consider limiting concurrent transfers per client

## Testing

1. Build and flash the ESP32 firmware
2. Run the web application
3. Navigate to the FileManager component
4. Test upload/download with files of various sizes

## Troubleshooting

**Transfer fails midway:**
- Check WebSocket connection stability
- Verify ESP32 has sufficient filesystem space
- Check for timeout issues

**Upload creates corrupted files:**
- Verify chunk order is preserved
- Check for protobuf encoding/decoding errors

**ESP32 runs out of memory:**
- Reduce number of concurrent transfers
- Close File handles properly after transfers
- Run cleanup more frequently
