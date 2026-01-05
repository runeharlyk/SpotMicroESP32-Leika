# Filesystem Chunked Transfer - Quick Start Guide

## 🚀 Quick Setup (5 minutes)

### 1. Build & Flash ESP32

```bash
# From project root
python esp32/scripts/compile_protos.py
pio run -e seeed-xiao-esp32s3 -t upload
```

### 2. Build & Run Client

```bash
cd app
pnpm proto
pnpm dev
```

### 3. Use the File Manager

Add to any Svelte page:

```svelte
<script>
  import FileManager from '$lib/components/filesystem/FileManager.svelte'
</script>

<FileManager />
```

## 📖 Common Operations

### Upload a File

```typescript
import { fileSystemClient } from '$lib/filesystem/chunkedTransfer'

// From browser File input
const file = event.target.files[0]
await fileSystemClient.uploadFileFromBrowser('/config/settings.json', file, (progress) => {
  console.log(`${progress.percentage.toFixed(1)}%`)
})
```

### Download a File

```typescript
await fileSystemClient.downloadFileAndSave('/config/settings.json', 'settings.json', (progress) => {
  console.log(`${progress.percentage.toFixed(1)}%`)
})
```

### List Directory

```typescript
const result = await fileSystemClient.listDirectory('/config')
console.log('Files:', result.files)
console.log('Directories:', result.directories)
```

### Create Directory

```typescript
await fileSystemClient.createDirectory('/config/backups')
```

### Delete File or Directory

```typescript
await fileSystemClient.deleteFile('/config/old_settings.json')
```

## 🎯 Integration Points

### ESP32 Side

All handlers are already integrated in [esp32/src/main.cpp](esp32/src/main.cpp:191-238):
- ✅ Correlation handlers registered
- ✅ Periodic cleanup task added (every 60s)
- ✅ FileSystemWS::fsHandler initialized

### Client Side

Import and use anywhere in your Svelte app:

```typescript
import { fileSystemClient } from '$lib/filesystem/chunkedTransfer'
```

## 🔧 Key Configuration

### ESP32 ([esp32/include/filesystem_ws.h](esp32/include/filesystem_ws.h))
```cpp
#define FS_MAX_CHUNK_SIZE 1024      // 1KB chunks
#define FS_TRANSFER_TIMEOUT 30000   // 30 second timeout
```

### Client ([app/src/lib/filesystem/chunkedTransfer.ts](app/src/lib/filesystem/chunkedTransfer.ts:12))
```typescript
const MAX_CHUNK_SIZE = 1024  // Must match ESP32
```

## ✅ Verification

### Test ESP32 Build
```bash
pio run -e seeed-xiao-esp32s3
```

### Test Client Build
```bash
cd app && pnpm build
```

### Test Runtime
1. Open browser to http://esp32-ip/
2. Navigate to File Manager component
3. Try uploading a small file
4. Try downloading it back
5. Check ESP32 serial output for logs

## 📊 What Works

- ✅ Files of any size (chunked automatically)
- ✅ Progress tracking
- ✅ Multiple concurrent transfers
- ✅ Automatic error recovery
- ✅ Transfer cancellation
- ✅ Directory operations

## 🐛 Quick Troubleshooting

**Build fails on ESP32?**
→ Run `python esp32/scripts/compile_protos.py`

**TypeScript errors in client?**
→ Run `cd app && pnpm proto`

**Transfer fails midway?**
→ Check WebSocket connection stability

**"Invalid transfer ID" error?**
→ Transfer timed out, start a new one

**Corrupted files after upload?**
→ Verify chunk size matches on both sides

## 📚 Documentation

- [FILESYSTEM_INTEGRATION_SUMMARY.md](FILESYSTEM_INTEGRATION_SUMMARY.md) - Complete integration details
- [FILESYSTEM_CHUNKED_TRANSFER.md](FILESYSTEM_CHUNKED_TRANSFER.md) - Full documentation
- [app/src/lib/components/filesystem/FileManager.svelte](app/src/lib/components/filesystem/FileManager.svelte) - Example UI implementation

## 💡 Pro Tips

1. **Large Files**: Upload/download works great for files up to several MB
2. **Progress Callbacks**: Use them to show user feedback
3. **Error Handling**: Always check `result.success` and handle `result.error`
4. **Path Safety**: Validate paths on client before sending to ESP32
5. **Cleanup**: Cancel transfers if user navigates away

## 🎨 Example UI Integration

```svelte
<script lang="ts">
  import { fileSystemClient } from '$lib/filesystem/chunkedTransfer'

  let uploading = false
  let progress = 0

  async function handleUpload(event: Event) {
    const file = (event.target as HTMLInputElement).files?.[0]
    if (!file) return

    uploading = true
    const result = await fileSystemClient.uploadFileFromBrowser(
      `/uploads/${file.name}`,
      file,
      (p) => { progress = p.percentage }
    )
    uploading = false

    if (result.success) {
      alert('Upload complete!')
    } else {
      alert(`Upload failed: ${result.error}`)
    }
  }
</script>

<input type="file" on:change={handleUpload} disabled={uploading} />
{#if uploading}
  <progress value={progress} max="100">{progress.toFixed(1)}%</progress>
{/if}
```

---

**That's it!** You now have a fully functional chunked file transfer system. 🎉
