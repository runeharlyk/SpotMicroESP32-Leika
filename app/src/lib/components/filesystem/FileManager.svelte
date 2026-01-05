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
			error = e instanceof Error ? e.message : 'Unknown error'
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

		const destinationPath = currentPath.endsWith('/')
			? currentPath + file.name
			: currentPath + '/' + file.name

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

		const filePath = currentPath.endsWith('/')
			? currentPath + filename
			: currentPath + '/' + filename

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
		const path = currentPath.endsWith('/') ? currentPath + name : currentPath + '/' + name

		try {
			const result = await fileSystemClient.deleteFile(path)
			if (result.success) {
				await loadDirectory()
			} else {
				error = result.error || 'Delete failed'
			}
		} catch (e) {
			error = e instanceof Error ? e.message : 'Delete error'
		}
	}

	async function handleCreateDirectory() {
		const name = prompt('Enter directory name:')
		if (!name) return

		error = ''
		const path = currentPath.endsWith('/') ? currentPath + name : currentPath + '/' + name

		try {
			const result = await fileSystemClient.createDirectory(path)
			if (result.success) {
				await loadDirectory()
			} else {
				error = result.error || 'Failed to create directory'
			}
		} catch (e) {
			error = e instanceof Error ? e.message : 'Error creating directory'
		}
	}

	function formatBytes(bytes: number): string {
		if (bytes === 0) return '0 B'
		const k = 1024
		const sizes = ['B', 'KB', 'MB', 'GB']
		const i = Math.floor(Math.log(bytes) / Math.log(k))
		return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
	}

	onMount(() => {
		loadDirectory()
	})
</script>

<div class="file-manager">
	<div class="toolbar">
		<h2>File Manager</h2>
		<div class="path">Current: {currentPath}</div>
		<div class="actions">
			<button on:click={handleCreateDirectory}>New Folder</button>
			<label class="upload-btn">
				Upload File
				<input type="file" on:change={handleFileUpload} style="display: none;" />
			</label>
			<button on:click={loadDirectory}>Refresh</button>
		</div>
	</div>

	{#if error}
		<div class="error">{error}</div>
	{/if}

	{#if uploadProgress}
		<div class="progress">
			<div class="progress-label">
				Uploading: {uploadProgress.percentage.toFixed(1)}% ({formatBytes(
					uploadProgress.bytesTransferred
				)} / {formatBytes(uploadProgress.totalBytes)})
			</div>
			<div class="progress-bar">
				<div class="progress-fill" style="width: {uploadProgress.percentage}%"></div>
			</div>
		</div>
	{/if}

	{#if downloadProgress}
		<div class="progress">
			<div class="progress-label">
				Downloading: {downloadProgress.percentage.toFixed(1)}% ({formatBytes(
					downloadProgress.bytesTransferred
				)} / {formatBytes(downloadProgress.totalBytes)})
			</div>
			<div class="progress-bar">
				<div class="progress-fill" style="width: {downloadProgress.percentage}%"></div>
			</div>
		</div>
	{/if}

	<div class="file-list">
		{#if loading}
			<div class="loading">Loading...</div>
		{:else}
			{#if currentPath !== '/'}
				<div class="file-item directory" on:click={() => navigateTo('/')}>
					<span class="icon">📁</span>
					<span class="name">..</span>
				</div>
			{/if}

			{#each directories as dir}
				<div class="file-item directory">
					<span class="icon">📁</span>
					<span class="name" on:click={() => navigateTo(currentPath + '/' + dir.name)}
						>{dir.name}</span
					>
					<button class="delete-btn" on:click={() => handleDelete(dir.name, true)}>Delete</button>
				</div>
			{/each}

			{#each files as file}
				<div class="file-item">
					<span class="icon">📄</span>
					<span class="name">{file.name}</span>
					<span class="size">{formatBytes(file.size)}</span>
					<button class="download-btn" on:click={() => handleDownload(file.name)}>Download</button>
					<button class="delete-btn" on:click={() => handleDelete(file.name, false)}>Delete</button>
				</div>
			{/each}

			{#if files.length === 0 && directories.length === 0}
				<div class="empty">Directory is empty</div>
			{/if}
		{/if}
	</div>
</div>

<style>
	.file-manager {
		max-width: 800px;
		margin: 2rem auto;
		padding: 1rem;
		border: 1px solid #ddd;
		border-radius: 8px;
		background: white;
	}

	.toolbar {
		margin-bottom: 1rem;
	}

	.toolbar h2 {
		margin: 0 0 0.5rem 0;
	}

	.path {
		font-family: monospace;
		background: #f5f5f5;
		padding: 0.5rem;
		border-radius: 4px;
		margin-bottom: 0.5rem;
	}

	.actions {
		display: flex;
		gap: 0.5rem;
	}

	.actions button,
	.upload-btn {
		padding: 0.5rem 1rem;
		background: #007bff;
		color: white;
		border: none;
		border-radius: 4px;
		cursor: pointer;
	}

	.actions button:hover,
	.upload-btn:hover {
		background: #0056b3;
	}

	.error {
		background: #f8d7da;
		color: #721c24;
		padding: 0.75rem;
		border-radius: 4px;
		margin-bottom: 1rem;
	}

	.progress {
		margin-bottom: 1rem;
	}

	.progress-label {
		margin-bottom: 0.5rem;
		font-size: 0.9rem;
	}

	.progress-bar {
		height: 20px;
		background: #e9ecef;
		border-radius: 4px;
		overflow: hidden;
	}

	.progress-fill {
		height: 100%;
		background: #28a745;
		transition: width 0.3s ease;
	}

	.file-list {
		border: 1px solid #ddd;
		border-radius: 4px;
		min-height: 200px;
	}

	.file-item {
		display: flex;
		align-items: center;
		padding: 0.75rem;
		border-bottom: 1px solid #eee;
		gap: 0.5rem;
	}

	.file-item:last-child {
		border-bottom: none;
	}

	.file-item.directory {
		background: #f8f9fa;
	}

	.icon {
		font-size: 1.5rem;
	}

	.name {
		flex: 1;
		cursor: pointer;
	}

	.name:hover {
		text-decoration: underline;
	}

	.size {
		color: #6c757d;
		font-size: 0.9rem;
	}

	.download-btn,
	.delete-btn {
		padding: 0.25rem 0.75rem;
		border: none;
		border-radius: 4px;
		cursor: pointer;
		font-size: 0.9rem;
	}

	.download-btn {
		background: #28a745;
		color: white;
	}

	.download-btn:hover {
		background: #218838;
	}

	.delete-btn {
		background: #dc3545;
		color: white;
	}

	.delete-btn:hover {
		background: #c82333;
	}

	.loading,
	.empty {
		text-align: center;
		padding: 2rem;
		color: #6c757d;
	}
</style>
