<script lang="ts">
    import Spinner from '$lib/components/Spinner.svelte'
    import Folder from './Folder.svelte'
    import { api } from '$lib/api'
    import { FolderIcon, Add, FileIcon } from '$lib/components/icons'
    import { modals } from 'svelte-modals'
    import NewFolderDialog from './NewFolderDialog.svelte'
    import NewFileDialog from './NewFileDialog.svelte'
    import {
        type FileListData,
        type FileDeleteRequest,
        type FileEditRequest,
        type FileMkdirRequest,
        type FileContentData,
        FileListData as FileListDataProto,
        FileDeleteRequest as FileDeleteRequestProto,
        FileEditRequest as FileEditRequestProto,
        FileMkdirRequest as FileMkdirRequestProto,
        FileContentData as FileContentDataProto
    } from '$lib/platform_shared/message'

    type Directory = {
        [key: string]: number | Directory
    }

    let filename = $state('')
    let content = $state('')
    let isEditing = $state(false)

    function convertFileListToDirectory(fileList: FileListData): { root: Directory } {
        const root: Directory = {}
        for (const file of fileList.files) {
            if (file.isDirectory) {
                root[file.name] = {}
            } else {
                root[file.name] = file.size
            }
        }
        return { root }
    }

    const getFiles = async (): Promise<{ root: Directory }> => {
        const result = await api.get('/api/files', FileListDataProto)
        if (result.isOk()) {
            return convertFileListToDirectory(result.inner)
        }
        return { root: {} }
    }

    const getContent = async (name: string) => {
        if (!name) return ''
        const result = await api.get(`/api/config/${name}`, FileContentDataProto)
        if (result.isOk()) {
            content = result.inner.content
            return content
        }
        return ''
    }

    const saveContent = async () => {
        if (!filename) return
        const request: FileEditRequest = {
            file: '/config/' + filename,
            content
        }
        const result = await api.postNoResponse('/api/files/edit', request, FileEditRequestProto)
        if (result.isOk()) {
            isEditing = false
        }
    }

    const deleteFile = async (name: string) => {
        if (!confirm(`Are you sure you want to delete ${name}?`)) return
        const request: FileDeleteRequest = { file: '/config/' + name }
        const result = await api.postNoResponse('/api/files/delete', request, FileDeleteRequestProto)
        if (result.isOk()) {
            filename = ''
            content = ''
        }
    }

    const createFolder = async (folderName: string) => {
        if (!folderName) return
        const request: FileMkdirRequest = { path: '/config/' + folderName }
        const result = await api.postNoResponse('/api/files/mkdir', request, FileMkdirRequestProto)
        if (result.isOk()) {
            await getFiles()
        }
    }

    const updateSelected = async (name: string) => {
        filename = name
        isEditing = false
        await getContent(name)
    }

    const openNewFolderDialog = () => {
        modals.open(NewFolderDialog, {
            onConfirm: createFolder
        })
    }

    const createFile = async (fileName: string) => {
        if (!fileName) return
        const request: FileEditRequest = {
            file: '/config/' + fileName,
            content: '{}'
        }
        const result = await api.postNoResponse('/api/files/edit', request, FileEditRequestProto)
        if (result.isOk()) {
            await getFiles()
            await updateSelected(fileName)
        }
    }

    const openNewFileDialog = () => {
        modals.open(NewFileDialog, {
            onConfirm: createFile
        })
    }
</script>

<FolderIcon class="flex-shrink-0 mr-2 h-6 w-6 self-end" />
<div class="flex justify-between items-center w-full gap-2">
    <span>File System</span>
    <div class="flex gap-2">
        <button class="btn btn-sm btn-primary flex items-center gap-2" onclick={openNewFileDialog}>
            <FileIcon class="w-4 h-4" />
            New File
        </button>
        <button
            class="btn btn-sm btn-primary flex items-center gap-2"
            onclick={openNewFolderDialog}
        >
            <Add class="w-4 h-4" />
            New Folder
        </button>
    </div>
</div>

<div class="flex flex-col md:flex-row gap-4 w-full">
    <div
        class="w-full md:w-[300px] md:min-w-[300px] md:max-w-[300px] border-b md:border-b-0 md:border-r pb-4 md:pb-0 md:pr-4"
    >
        {#await getFiles()}
            <Spinner />
        {:then files}
            <Folder
                name="/"
                files={files.root}
                expanded
                selected={updateSelected}
                onDelete={deleteFile}
            />
        {/await}
    </div>

    <div class="flex-1 min-w-0">
        {#if filename}
            <div
                class="flex flex-col sm:flex-row justify-between items-start sm:items-center mb-4 gap-2"
            >
                <h3 class="text-lg font-semibold truncate">{filename}</h3>
                <div class="flex gap-2">
                    {#if isEditing}
                        <button class="btn btn-sm btn-primary" onclick={saveContent}>Save</button>
                        <button
                            class="btn btn-sm btn-secondary"
                            onclick={() => (isEditing = false)}
                        >
                            Cancel
                        </button>
                    {:else}
                        <button class="btn btn-sm btn-primary" onclick={() => (isEditing = true)}>
                            Edit
                        </button>
                        <button class="btn btn-sm btn-danger" onclick={() => deleteFile(filename)}>
                            Delete
                        </button>
                    {/if}
                </div>
            </div>

            {#await getContent(filename)}
                <Spinner />
            {:then}
                {#if isEditing}
                    <textarea
                        class="w-full h-[300px] sm:h-[500px] font-mono p-2 bg-gray-800 text-white"
                        bind:value={content}
                    ></textarea>
                {:else}
                    <pre
                        class="bg-gray-800 p-4 rounded overflow-auto max-h-[300px] sm:max-h-[500px]">{content}</pre>
                {/if}
            {/await}
        {:else}
            <div class="text-center text-gray-500">Select a file to view its contents</div>
        {/if}
    </div>
</div>
