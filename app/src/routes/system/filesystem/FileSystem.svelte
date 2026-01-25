<script lang="ts">
	import Spinner from '$lib/components/Spinner.svelte'
	import { fileSystemClient } from '$lib/filesystem/chunkedTransfer'
	import type { TransferProgress } from '$lib/types/models'
	import { FolderIcon, Add, FileIcon, UploadIcon, DownloadIcon, TrashIcon } from '$lib/components/icons'
	import { modals } from 'svelte-modals'
	import NewFolderDialog from './NewFolderDialog.svelte'
	import NewFileDialog from './NewFileDialog.svelte'
	import { api } from '$lib/api'
	import type { Response } from '$lib/platform_shared/api'

	let currentPath = $state('/')
	let files = $state<Array<{ name: string; size: number }>>([])
	let directories = $state<Array<{ name: string }>>([])
	let loading = $state(false)
	let error = $state('')

	let selectedFile = $state('')
	let fileContent = $state('')
	let isEditing = $state(false)
	let fileLoading = $state(false)

	let uploadProgress = $state<TransferProgress | null>(null)
	let downloadProgress = $state<TransferProgress | null>(null)
	let uploadInputRef: HTMLInputElement

	async function loadDirectory(path: string = currentPath) {
		loading = true
		error = ''
		try {
			const result = await fileSystemClient.listDirectory(path)
			if (result.success) {
				files = result.files
				directories = result.directories
				currentPath = path
			} else {
				error = result.error || 'Failed to load directory'
			}
		} catch (e) {
			error = e instanceof Error ? e.message : 'Unknown error'
		} finally {
			loading = false
		}
	}

	async function navigateTo(dirName: string) {
		const newPath = currentPath === '/' ? `/${dirName}` : `${currentPath}/${dirName}`
		await loadDirectory(newPath)
		selectedFile = ''
		fileContent = ''
	}

	async function navigateUp() {
		if (currentPath === '/') return
		const parts = currentPath.split('/').filter(Boolean)
		parts.pop()
		const newPath = parts.length === 0 ? '/' : '/' + parts.join('/')
		await loadDirectory(newPath)
		selectedFile = ''
		fileContent = ''
	}

	async function loadFileContent(filename: string) {
		fileLoading = true
		error = ''
		try {
			const filePath = currentPath === '/' ? `/${filename}` : `${currentPath}/${filename}`
			const result = await fileSystemClient.downloadFile(filePath)

			if (result.success && result.data) {
				// Convert bytes to string (assuming UTF-8 text file)
				const decoder = new TextDecoder('utf-8')
				fileContent = decoder.decode(result.data)
				selectedFile = filename
				isEditing = false
			} else {
				error = result.error || 'Failed to load file'
			}
		} catch (e) {
			error = e instanceof Error ? e.message : 'Failed to load file'
		} finally {
			fileLoading = false
		}
	}

	async function saveFileContent() {
		if (!selectedFile) return

		error = ''
		try {
			const filePath = currentPath === '/' ? `/${selectedFile}` : `${currentPath}/${selectedFile}`
			const data = new TextEncoder().encode(fileContent)

			const result = await fileSystemClient.uploadFile(filePath, data)

			if (result.success) {
				isEditing = false
				await loadDirectory() // Refresh to update file sizes
			} else {
				error = result.error || 'Failed to save file'
			}
		} catch (e) {
			error = e instanceof Error ? e.message : 'Failed to save file'
		}
	}

	async function handleFileUpload(event: Event) {
		const input = event.target as HTMLInputElement
		const file = input.files?.[0]
		if (!file) return

		uploadProgress = null
		error = ''

		const destinationPath = currentPath === '/'
			? `/${file.name}`
			: `${currentPath}/${file.name}`

		try {
			const result = await fileSystemClient.uploadFileFromBrowser(
				destinationPath,
				file,
				(progress) => {
					uploadProgress = progress
				}
			)

			if (result.success) {
				await loadDirectory()
			} else {
				error = result.error || 'Upload failed'
			}
		} catch (e) {
			error = e instanceof Error ? e.message : 'Upload error'
		} finally {
			uploadProgress = null
			input.value = ''
		}
	}

	async function handleDownload(filename: string) {
		downloadProgress = null
		error = ''

		const filePath = currentPath === '/'
			? `/${filename}`
			: `${currentPath}/${filename}`

		try {
			const result = await fileSystemClient.downloadFileAndSave(filePath, filename, (progress) => {
				downloadProgress = progress
			})

			if (!result.success) {
				error = result.error || 'Download failed'
			}
		} catch (e) {
			error = e instanceof Error ? e.message : 'Download error'
		} finally {
			downloadProgress = null
		}
	}

	async function handleDelete(name: string, isDirectory: boolean) {
		if (!confirm(`Delete ${isDirectory ? 'directory' : 'file'} "${name}"?`)) return

		error = ''
		const path = currentPath === '/' ? `/${name}` : `${currentPath}/${name}`

		try {
			const result = await fileSystemClient.deleteFile(path)
			if (result.success) {
				if (selectedFile === name) {
					selectedFile = ''
					fileContent = ''
				}
				await loadDirectory()
			} else {
				error = result.error || 'Delete failed'
			}
		} catch (e) {
			error = e instanceof Error ? e.message : 'Delete error'
		}
	}

	async function createFolder(folderName: string) {
		if (!folderName) return

		error = ''
		const path = currentPath === '/' ? `/${folderName}` : `${currentPath}/${folderName}`

		try {
			const result = await api.post_proto<Response>('/api/files/mkdir', {
				fileMkdirRequest: { path }
			})

			if (result.isOk() && result.inner.statusCode === 200) {
				await loadDirectory()
			} else if (result.isErr()) {
				error = 'Failed to create directory'
			} else {
				error = result.inner.errorMessage || 'Failed to create directory'
			}
		} catch (e) {
			error = e instanceof Error ? e.message : 'Error creating directory'
		}
	}

	async function createFile(fileName: string) {
		if (!fileName) return

		error = ''
		const path = currentPath === '/' ? `/${fileName}` : `${currentPath}/${fileName}`

		try {
			const result = await fileSystemClient.uploadFile(path, new Uint8Array(0))

			if (result.success) {
				await loadDirectory()
				await loadFileContent(fileName)
			} else {
				error = result.error || 'Failed to create file'
			}
		} catch (e) {
			error = e instanceof Error ? e.message : 'Error creating file'
		}
	}

	function openNewFolderDialog() {
		modals.open(NewFolderDialog, {
			onConfirm: createFolder
		})
	}

	function openNewFileDialog() {
		modals.open(NewFileDialog, {
			onConfirm: createFile
		})
	}

	function formatBytes(bytes: number): string {
		if (bytes === 0) return '0 B'
		const k = 1024
		const sizes = ['B', 'KB', 'MB', 'GB']
		const i = Math.floor(Math.log(bytes) / Math.log(k))
		return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
	}

	// Load initial directory
	$effect(() => {
		loadDirectory('/')
	})
</script>

<FolderIcon class="flex-shrink-0 mr-2 h-6 w-6 self-end" />

<div class="flex justify-between items-center w-full gap-2 mb-4">
	<span class="text-xl font-bold">File System</span>
	<div class="flex gap-2">
		<button class="btn btn-sm btn-primary flex items-center gap-2" onclick={() => uploadInputRef.click()}>
			<UploadIcon class="w-4 h-4" />
			Upload File
		</button>
		<button class="btn btn-sm btn-primary flex items-center gap-2" onclick={openNewFileDialog}>
			<FileIcon class="w-4 h-4" />
			New File
		</button>
		<button class="btn btn-sm btn-primary flex items-center gap-2" onclick={openNewFolderDialog}>
			<Add class="w-4 h-4" />
			New Folder
		</button>
	</div>
</div>

<input
	type="file"
	bind:this={uploadInputRef}
	onchange={handleFileUpload}
	style="display: none;"
/>

{#if error}
	<div class="alert alert-error mb-4">
		<span>{error}</span>
	</div>
{/if}

{#if uploadProgress}
	<div class="mb-4">
		<div class="flex justify-between text-sm mb-1">
			<span>Uploading...</span>
			<span>{uploadProgress.percentage.toFixed(1)}% ({formatBytes(uploadProgress.bytesTransferred)} / {formatBytes(uploadProgress.totalBytes)})</span>
		</div>
		<progress class="progress progress-primary w-full" value={uploadProgress.percentage} max="100"></progress>
	</div>
{/if}

{#if downloadProgress}
	<div class="mb-4">
		<div class="flex justify-between text-sm mb-1">
			<span>Downloading...</span>
			<span>{downloadProgress.percentage.toFixed(1)}% ({formatBytes(downloadProgress.bytesTransferred)} / {formatBytes(downloadProgress.totalBytes)})</span>
		</div>
		<progress class="progress progress-primary w-full" value={downloadProgress.percentage} max="100"></progress>
	</div>
{/if}

<div class="flex flex-col md:flex-row gap-4 w-full">
	<!-- File Tree -->
	<div class="w-full md:w-[300px] md:min-w-[300px] md:max-w-[300px] border-b md:border-b-0 md:border-r pb-4 md:pb-0 md:pr-4">
		<!-- Current Path -->
		<div class="mb-4 p-2 bg-base-200 rounded font-mono text-sm flex items-center justify-between">
			<span class="truncate">{currentPath}</span>
			{#if currentPath !== '/'}
				<button class="btn btn-xs btn-ghost" onclick={navigateUp}>
					↑ Up
				</button>
			{/if}
		</div>

		{#if loading}
			<Spinner />
		{:else}
			<!-- Directories -->
			{#each directories as dir (dir.name)}
				<div class="flex items-center py-1 px-2 hover:bg-base-200 rounded group">
					<button class="flex items-center gap-2 flex-1" onclick={() => navigateTo(dir.name)}>
						<FolderIcon class="w-5 h-5 text-yellow-500" />
						<span class="text-sm">{dir.name}</span>
					</button>
					<button
						class="opacity-0 group-hover:opacity-100 btn btn-xs btn-ghost btn-square"
						onclick={() => handleDelete(dir.name, true)}
					>
						<TrashIcon class="w-4 h-4 text-error" />
					</button>
				</div>
			{/each}

			<!-- Files -->
			{#each files as file (file.name)}
				<div class="flex items-center py-1 px-2 hover:bg-base-200 rounded group">
					<button
						class="flex items-center gap-2 flex-1 min-w-0"
						onclick={() => loadFileContent(file.name)}
						class:font-bold={selectedFile === file.name}
					>
						<FileIcon class="w-4 h-4 flex-shrink-0" />
						<span class="text-sm truncate">{file.name}</span>
						<span class="text-xs opacity-60 ml-auto flex-shrink-0">{formatBytes(file.size)}</span>
					</button>
					<div class="flex gap-1 opacity-0 group-hover:opacity-100 flex-shrink-0">
						<button
							class="btn btn-xs btn-ghost btn-square"
							onclick={() => handleDownload(file.name)}
							title="Download"
						>
							<DownloadIcon class="w-4 h-4 text-info" />
						</button>
						<button
							class="btn btn-xs btn-ghost btn-square"
							onclick={() => handleDelete(file.name, false)}
							title="Delete"
						>
							<TrashIcon class="w-4 h-4 text-error" />
						</button>
					</div>
				</div>
			{/each}

			{#if files.length === 0 && directories.length === 0}
				<div class="text-center text-base-content/50 py-8">
					Directory is empty
				</div>
			{/if}
		{/if}
	</div>

	<!-- File Content -->
	<div class="flex-1 min-w-0">
		{#if selectedFile}
			<div class="flex flex-col sm:flex-row justify-between items-start sm:items-center mb-4 gap-2">
				<h3 class="text-lg font-semibold truncate">{selectedFile}</h3>
				<div class="flex gap-2">
					{#if isEditing}
						<button class="btn btn-sm btn-primary" onclick={saveFileContent}>
							Save
						</button>
						<button class="btn btn-sm btn-ghost" onclick={() => {
							isEditing = false
							loadFileContent(selectedFile)
						}}>
							Cancel
						</button>
					{:else}
						<button class="btn btn-sm btn-primary" onclick={() => (isEditing = true)}>
							Edit
						</button>
						<button class="btn btn-sm btn-ghost" onclick={() => handleDownload(selectedFile)}>
							<DownloadIcon class="w-4 h-4 mr-1" />
							Download
						</button>
						<button class="btn btn-sm btn-error" onclick={() => handleDelete(selectedFile, false)}>
							Delete
						</button>
					{/if}
				</div>
			</div>

			{#if fileLoading}
				<Spinner />
			{:else if isEditing}
				<textarea
					class="textarea textarea-bordered w-full h-[300px] sm:h-[500px] font-mono text-sm"
					bind:value={fileContent}
				></textarea>
			{:else}
				<pre class="bg-base-200 p-4 rounded overflow-auto max-h-[300px] sm:max-h-[500px] text-sm">{fileContent}</pre>
			{/if}
		{:else}
			<div class="text-center text-base-content/50 py-16">
				Select a file to view its contents
			</div>
		{/if}
	</div>
</div>
