# Adding New Proto Files

## Step-by-Step Guide

### 1. Create the new .proto file

Create `platform_shared/myfeature.proto`:

```protobuf
syntax = "proto3";

package socket_message;

message MyFeatureRequest {
    string name = 1;
}

message MyFeatureResponse {
    bool success = 1;
    string error = 2;
}
```

### 2. Create the .options file (for nanopb size constraints)

Create `platform_shared/myfeature.options`:

```
socket_message.MyFeatureRequest.name max_size:64
socket_message.MyFeatureResponse.error max_size:128
```

### 3. Import in message.proto

Add the import at the top of `platform_shared/message.proto`:

```protobuf
import "myfeature.proto";
```

### 4. Add to Message oneof (if needed for streaming/pub-sub)

If your message needs to be sent directly (not via correlation request/response), add it to the `Message` oneof in `message.proto`:

```protobuf
message Message {
    oneof message {
        // ... existing fields ...
        MyFeatureRequest my_feature_request = 300;  // Pick unused tag number
        MyFeatureResponse my_feature_response = 301;
    }
}
```

### 5. Add to CorrelationRequest/Response (if using request/response pattern)

For request/response messages, add to the correlation oneofs in `message.proto`:

```protobuf
message CorrelationRequest {
    oneof request {
        // ... existing fields ...
        MyFeatureRequest my_feature_request = 200;  // Pick unused tag number
    }
}

message CorrelationResponse {
    oneof response {
        // ... existing fields ...
        MyFeatureResponse my_feature_response = 200;
    }
}
```

### 6. Update compile scripts

**ESP32 (esp32/scripts/compile_protos.py):**

```python
proto_files = [proto_dir / "filesystem.proto", proto_dir / "myfeature.proto", proto_dir / "message.proto"]
```

**TypeScript (app/scripts/compile_protos.js):**

```javascript
const protoFiles = ['filesystem.proto', 'myfeature.proto', 'message.proto']
```

### 7. Update socket.ts (for TypeScript - only if added to Message oneof)

If you added messages to the `Message` oneof, import the protoMetadata in `app/src/lib/stores/socket.ts`:

```typescript
import { protoMetadata as myfeatureProtoMetadata } from '$lib/platform_shared/myfeature'

// Add to combinedReferences
const combinedReferences: Record<string, MessageFns<unknown>> = {
    ...protoMetadata.references,
    ...filesystemProtoMetadata.references,
    ...myfeatureProtoMetadata.references  // Add this
}
```

### 8. Add MessageTraits (for ESP32 - only if added to Message oneof)

If you added messages to the `Message` oneof, add traits in `esp32/include/communication/proto_helpers.h`:

```cpp
// Before #undef DEFINE_MESSAGE_TRAITS
DEFINE_MESSAGE_TRAITS(MyFeatureRequest, my_feature_request)
DEFINE_MESSAGE_TRAITS(MyFeatureResponse, my_feature_response)
```

### 9. Build and test

```bash
# ESP32
pio run

# TypeScript
cd app && pnpm proto
```

## Quick Reference

| File | Purpose |
|------|---------|
| `platform_shared/*.proto` | Protocol definitions |
| `platform_shared/*.options` | Nanopb size constraints |
| `esp32/scripts/compile_protos.py` | ESP32 proto compilation |
| `app/scripts/compile_protos.js` | TypeScript proto compilation |
| `app/src/lib/stores/socket.ts` | Tag mapping for socket.on/emit |
| `esp32/include/communication/proto_helpers.h` | MessageTraits for ESP32 emit |

## Notes

- Messages in `CorrelationRequest/Response` don't need MessageTraits or socket.ts updates
- Messages in `Message` oneof (for streaming/pub-sub) need both
- Always use the same `package socket_message;` in all proto files
- Tag numbers in oneofs must be unique across all fields
