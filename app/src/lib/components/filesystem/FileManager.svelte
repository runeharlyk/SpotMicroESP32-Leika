<script lang="ts">
    import { fileSystemClient, type TransferProgress } from '$lib/filesystem/chunkedTransfer'
    import { onMount } from 'svelte'

    let currentPath = '/'
    let files: Array<{ name: string; size: number }> = []
    let directories: Array<{ name: string }> = []
    let loading = false
    let error = ''
    let uploadProgress: TransferProgress | null = null
    let downloadProgress: TransferProgress | null = null

    const joinPath = (name: string) => (currentPath === '/' ? '/' + name : currentPath + '/' + name)
    const getError = (e: unknown, fallback: string) =>
        e instanceof Error ? e.message : (e as { error?: string })?.error || fallback

    async function loadDirectory() {
        loading = true
        error = ''
        try {
            const result = await fileSystemClient.listDirectory(currentPath)
            if (result.success) {
                files = result.files
                directories = result.directories
            } else {
                error = result.error || 'Failed to load directory'
            }
        } catch (e) {
            error = getError(e, 'Unknown error')
        } finally {
            loading = false
        }
    }

    async function navigateTo(path: string) {
        currentPath = path
        await loadDirectory()
    }

    async function handleFileUpload(event: Event) {
        const input = event.target as HTMLInputElement
        const file = input.files?.[0]
        if (!file) return

        uploadProgress = null
        error = ''

        try {
            const result = await fileSystemClient.uploadFileFromBrowser(
                joinPath(file.name),
                file,
                p => (uploadProgress = p)
            )
            if (result.success) await loadDirectory()
            else error = result.error || 'Upload failed'
        } catch (e) {
            error = getError(e, 'Upload error')
        } finally {
            uploadProgress = null
            input.value = ''
        }
    }

    async function handleDownload(filename: string) {
        downloadProgress = null
        error = ''

        try {
            const result = await fileSystemClient.downloadFileAndSave(
                joinPath(filename),
                filename,
                p => (downloadProgress = p)
            )
            if (!result.success) error = result.error || 'Download failed'
        } catch (e) {
            error = getError(e, 'Download error')
        } finally {
            downloadProgress = null
        }
    }

    async function handleDelete(name: string, isDirectory: boolean) {
        if (!confirm(`Delete ${isDirectory ? 'directory' : 'file'} "${name}"?`)) return
        error = ''

        try {
            const result = await fileSystemClient.deleteFile(joinPath(name))
            if (result.success) await loadDirectory()
            else error = result.error || 'Delete failed'
        } catch (e) {
            error = getError(e, 'Delete error')
        }
    }

    async function handleCreateDirectory() {
        const name = prompt('Enter directory name:')
        if (!name) return
        error = ''

        try {
            const result = await fileSystemClient.createDirectory(joinPath(name))
            if (result.success) await loadDirectory()
            else error = result.error || 'Failed to create directory'
        } catch (e) {
            error = getError(e, 'Error creating directory')
        }
    }

    function formatBytes(bytes: number): string {
        if (bytes === 0) return '0 B'
        const k = 1024
        const sizes = ['B', 'KB', 'MB', 'GB']
        const i = Math.floor(Math.log(bytes) / Math.log(k))
        return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
    }

    onMount(loadDirectory)
</script>

<div class="max-w-3xl mx-auto my-8 p-4 border border-gray-300 rounded-lg bg-white">
    <div class="mb-4">
        <h2 class="m-0 mb-2">File Manager</h2>
        <div class="font-mono bg-gray-100 p-2 rounded mb-2">Current: {currentPath}</div>
        <div class="flex gap-2">
            <button
                class="px-4 py-2 bg-blue-600 text-white rounded cursor-pointer hover:bg-blue-700"
                on:click={handleCreateDirectory}>New Folder</button
            >
            <label
                class="px-4 py-2 bg-blue-600 text-white rounded cursor-pointer hover:bg-blue-700"
            >
                Upload File
                <input type="file" on:change={handleFileUpload} class="hidden" />
            </label>
            <button
                class="px-4 py-2 bg-blue-600 text-white rounded cursor-pointer hover:bg-blue-700"
                on:click={loadDirectory}>Refresh</button
            >
        </div>
    </div>

    {#if error}
        <div class="bg-red-100 text-red-800 p-3 rounded mb-4">{error}</div>
    {/if}

    {#if uploadProgress}
        <div class="mb-4">
            <div class="mb-2 text-sm">
                Uploading: {uploadProgress.percentage.toFixed(1)}% ({formatBytes(
                    uploadProgress.bytesTransferred
                )} / {formatBytes(uploadProgress.totalBytes)})
            </div>
            <div class="h-5 bg-gray-200 rounded overflow-hidden">
                <div
                    class="h-full bg-green-600 transition-all duration-300"
                    style="width: {uploadProgress.percentage}%"
                ></div>
            </div>
        </div>
    {/if}

    {#if downloadProgress}
        <div class="mb-4">
            <div class="mb-2 text-sm">
                Downloading: {downloadProgress.percentage.toFixed(1)}% ({formatBytes(
                    downloadProgress.bytesTransferred
                )} / {formatBytes(downloadProgress.totalBytes)})
            </div>
            <div class="h-5 bg-gray-200 rounded overflow-hidden">
                <div
                    class="h-full bg-green-600 transition-all duration-300"
                    style="width: {downloadProgress.percentage}%"
                ></div>
            </div>
        </div>
    {/if}

    <div class="border border-gray-300 rounded min-h-[200px]">
        {#if loading}
            <div class="text-center p-8 text-gray-500">Loading...</div>
        {:else}
            {#if currentPath !== '/'}
                <div
                    class="flex items-center p-3 border-b border-gray-100 gap-2 bg-gray-50 cursor-pointer"
                    on:click={() => navigateTo('/')}
                >
                    <span class="text-2xl">📁</span>
                    <span class="flex-1 hover:underline">..</span>
                </div>
            {/if}

            {#each directories as dir}
                <div class="flex items-center p-3 border-b border-gray-100 gap-2 bg-gray-50">
                    <span class="text-2xl">📁</span>
                    <span
                        class="flex-1 cursor-pointer hover:underline"
                        on:click={() => navigateTo(currentPath + '/' + dir.name)}>{dir.name}</span
                    >
                    <button
                        class="px-3 py-1 bg-red-600 text-white rounded text-sm hover:bg-red-700"
                        on:click={() => handleDelete(dir.name, true)}>Delete</button
                    >
                </div>
            {/each}

            {#each files as file}
                <div class="flex items-center p-3 border-b border-gray-100 gap-2 last:border-b-0">
                    <span class="text-2xl">📄</span>
                    <span class="flex-1">{file.name}</span>
                    <span class="text-gray-500 text-sm">{formatBytes(file.size)}</span>
                    <button
                        class="px-3 py-1 bg-green-600 text-white rounded text-sm hover:bg-green-700"
                        on:click={() => handleDownload(file.name)}>Download</button
                    >
                    <button
                        class="px-3 py-1 bg-red-600 text-white rounded text-sm hover:bg-red-700"
                        on:click={() => handleDelete(file.name, false)}>Delete</button
                    >
                </div>
            {/each}

            {#if files.length === 0 && directories.length === 0}
                <div class="text-center p-8 text-gray-500">Directory is empty</div>
            {/if}
        {/if}
    </div>
</div>
