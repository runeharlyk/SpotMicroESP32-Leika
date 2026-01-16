import { socket } from '$lib/stores/socket'
import * as Messages from '$lib/platform_shared/message'
import type {
	FSDeleteRequest,
	FSMkdirRequest,
	FSListRequest,
	FSDownloadRequest,
	FSDownloadData,
	FSDownloadComplete,
	FSUploadStart,
	FSUploadData,
	FSUploadComplete,
	FSCancelTransfer
} from '$lib/platform_shared/message'

const MAX_CHUNK_SIZE = 2 ** 14 // ~= 16 kb

export interface FileInfo {
	name: string
	size: number
}

export interface DirectoryInfo {
	name: string
}

export interface ListResult {
	success: boolean
	error?: string
	files: FileInfo[]
	directories: DirectoryInfo[]
}

export interface TransferProgress {
	transferId: string
	bytesTransferred: number
	totalBytes: number
	chunksCompleted: number
	totalChunks: number
	percentage: number
}

export type ProgressCallback = (progress: TransferProgress) => void

// Active transfer tracking
interface ActiveDownload {
	path: string
	buffer: Uint8Array
	fileSize: number
	totalChunks: number
	chunksReceived: number
	bytesReceived: number
	resolve: (result: { success: boolean; data?: Uint8Array; error?: string }) => void
	reject: (error: Error) => void
	onProgress?: ProgressCallback
	timeoutId: ReturnType<typeof setTimeout>
}

interface ActiveUpload {
	path: string
	transferId: string
	totalChunks: number
	chunksSent: number
	resolve: (result: { success: boolean; error?: string }) => void
	reject: (error: Error) => void
	onProgress?: ProgressCallback
	timeoutId: ReturnType<typeof setTimeout>
}

export class FileSystemClient {
	private activeDownloads = new Map<string, ActiveDownload>()
	private activeUploads = new Map<string, ActiveUpload>()
	private downloadListenerCleanup: (() => void) | null = null
	private completeListenerCleanup: (() => void) | null = null
	private uploadCompleteListenerCleanup: (() => void) | null = null
	private transferTimeout = 60000 // 60 seconds timeout for transfers

	constructor() {
		this.setupListeners()
	}

	private setupListeners() {
		// Listen for download data chunks
		this.downloadListenerCleanup = socket.on(
			Messages.FSDownloadData,
			(data: FSDownloadData) => {
				this.handleDownloadData(data)
			}
		)

		// Listen for download completion
		this.completeListenerCleanup = socket.on(
			Messages.FSDownloadComplete,
			(complete: FSDownloadComplete) => {
				this.handleDownloadComplete(complete)
			}
		)

		// Listen for upload completion
		this.uploadCompleteListenerCleanup = socket.on(
			Messages.FSUploadComplete,
			(complete: FSUploadComplete) => {
				this.handleUploadComplete(complete)
			}
		)
	}

	private handleDownloadData(data: FSDownloadData) {
		const download = this.activeDownloads.get(data.transferId)
		if (!download) {
			console.warn(`Received download data for unknown transfer: ${data.transferId}`)
			return
		}

		// Reset timeout
		clearTimeout(download.timeoutId)
		download.timeoutId = setTimeout(() => {
			this.activeDownloads.delete(data.transferId)
			download.reject(new Error('Download timeout'))
		}, this.transferTimeout)

		// Copy chunk data to buffer
		if (data.data && data.data.length > 0) {
			const offset = data.chunkIndex * MAX_CHUNK_SIZE
			download.buffer.set(data.data, offset)
			download.bytesReceived += data.data.length
			download.chunksReceived++
		}

		// Report progress
		if (download.onProgress) {
			download.onProgress({
				transferId: data.transferId,
				bytesTransferred: download.bytesReceived,
				totalBytes: download.fileSize,
				chunksCompleted: download.chunksReceived,
				totalChunks: download.totalChunks,
				percentage: (download.chunksReceived / download.totalChunks) * 100
			})
		}
	}

	private handleDownloadComplete(complete: FSDownloadComplete) {
		const download = this.activeDownloads.get(complete.transferId)
		if (!download) {
			// This is normal for error cases where transferId wasn't set
			if (complete.error) {
				console.warn(`Download failed: ${complete.error}`)
			}
			return
		}

		clearTimeout(download.timeoutId)
		this.activeDownloads.delete(complete.transferId)

		if (complete.success) {
			// Trim buffer to actual file size
			const finalData = download.buffer.slice(0, complete.fileSize)
			download.resolve({ success: true, data: finalData })
		} else {
			download.resolve({ success: false, error: complete.error || 'Download failed' })
		}
	}

	private handleUploadComplete(complete: FSUploadComplete) {
		const upload = this.activeUploads.get(complete.transferId)
		if (!upload) {
			console.warn(`Received upload complete for unknown transfer: ${complete.transferId}`)
			return
		}

		clearTimeout(upload.timeoutId)
		this.activeUploads.delete(complete.transferId)

		if (complete.success) {
			upload.resolve({ success: true })
		} else {
			upload.resolve({ success: false, error: complete.error || 'Upload failed' })
		}
	}

	/**
	 * Delete a file or directory on the ESP32
	 */
	async deleteFile(path: string): Promise<{ success: boolean; error?: string }> {
		const request: FSDeleteRequest = { path }

		const response = await socket.request({
			fsDeleteRequest: request
		})

		if (response.fsDeleteResponse) {
			return {
				success: response.fsDeleteResponse.success,
				error: response.fsDeleteResponse.error || undefined
			}
		}

		return { success: false, error: 'No response received' }
	}

	/**
	 * Create a directory on the ESP32
	 */
	async createDirectory(path: string): Promise<{ success: boolean; error?: string }> {
		const request: FSMkdirRequest = { path }

		const response = await socket.request({
			fsMkdirRequest: request
		})

		if (response.fsMkdirResponse) {
			return {
				success: response.fsMkdirResponse.success,
				error: response.fsMkdirResponse.error || undefined
			}
		}

		return { success: false, error: 'No response received' }
	}

	/**
	 * List files and directories at the given path
	 */
	async listDirectory(path: string = '/'): Promise<ListResult> {
		const request: FSListRequest = { path }

		const response = await socket.request({
			fsListRequest: request
		})

		if (response.fsListResponse) {
			const resp = response.fsListResponse
			return {
				success: resp.success,
				error: resp.error || undefined,
				files: (resp.files || []).map((f) => ({ name: f.name, size: f.size })),
				directories: (resp.directories || []).map((d) => ({ name: d.name }))
			}
		}

		return { success: false, error: 'No response received', files: [], directories: [] }
	}

	/**
	 * Download a file from the ESP32 using streaming transfer
	 * Server streams all chunks without waiting for ACKs
	 */
	async downloadFile(
		path: string,
		onProgress?: ProgressCallback
	): Promise<{ success: boolean; data?: Uint8Array; error?: string }> {
		return new Promise((resolve, reject) => {
			// Send download request - server will stream chunks back
			const request: FSDownloadRequest = { path }

			// We need to set up tracking before sending the request
			// The server will generate a transfer ID and include it in all responses
			// We'll capture the first chunk to get the transfer ID

			// Set up timeout for initial response
			const initialTimeout = setTimeout(() => {
				reject(new Error('Download request timeout - no data received'))
			}, this.transferTimeout)

			// One-time listener for the first chunk to get transfer details
			const firstChunkHandler = (data: FSDownloadData) => {
				clearTimeout(initialTimeout)

				// Now we have the real transfer ID
				const transferId = data.transferId

				// Estimate total size from first chunk (server sends file_size in complete message)
				// For now, allocate a large buffer and resize later
				const estimatedSize = 10 * 1024 * 1024 // 10MB max
				const buffer = new Uint8Array(estimatedSize)

				const download: ActiveDownload = {
					path,
					buffer,
					fileSize: estimatedSize, // Will be updated on completion
					totalChunks: Math.ceil(estimatedSize / MAX_CHUNK_SIZE),
					chunksReceived: 0,
					bytesReceived: 0,
					resolve,
					reject,
					onProgress,
					timeoutId: setTimeout(() => {
						this.activeDownloads.delete(transferId)
						reject(new Error('Download timeout'))
					}, this.transferTimeout)
				}

				this.activeDownloads.set(transferId, download)

				// Process this first chunk
				this.handleDownloadData(data)

				// Remove the first chunk handler - subsequent chunks go through normal listener
				firstChunkCleanup()
			}

			// Error handler for if download fails immediately
			const errorHandler = (complete: FSDownloadComplete) => {
				if (!complete.success && !complete.transferId) {
					clearTimeout(initialTimeout)
					firstChunkCleanup()
					errorCleanup()
					resolve({ success: false, error: complete.error || 'Download failed' })
				}
			}

			const firstChunkCleanup = socket.on(Messages.FSDownloadData, firstChunkHandler)
			const errorCleanup = socket.on(Messages.FSDownloadComplete, errorHandler)

			// Send the download request (no response expected, server streams data)
			socket.request({ fsDownloadRequest: request }).catch((err) => {
				clearTimeout(initialTimeout)
				firstChunkCleanup()
				errorCleanup()
				reject(err)
			})
		})
	}

	/**
	 * Upload a file to the ESP32 using streaming transfer
	 * Client sends all chunks without waiting for ACKs
	 */
	async uploadFile(
		path: string,
		data: Uint8Array,
		onProgress?: ProgressCallback
	): Promise<{ success: boolean; error?: string }> {
		const fileSize = data.length
		const chunkSize = MAX_CHUNK_SIZE
		const totalChunks = Math.ceil(fileSize / chunkSize) || 1

		// Start upload - get transfer ID
		const startRequest: FSUploadStart = {
			path,
			fileSize,
			totalChunks
		}

		const startResponse = await socket.request({
			fsUploadStart: startRequest
		})

		if (!startResponse.fsUploadStartResponse) {
			return { success: false, error: 'Failed to start upload' }
		}

		const startResp = startResponse.fsUploadStartResponse

		if (!startResp.success) {
			return { success: false, error: startResp.error || 'Failed to start upload' }
		}

		const transferId = startResp.transferId

		return new Promise((resolve, reject) => {
			// Set up upload tracking
			const upload: ActiveUpload = {
				path,
				transferId,
				totalChunks,
				chunksSent: 0,
				resolve,
				reject,
				onProgress,
				timeoutId: setTimeout(() => {
					this.activeUploads.delete(transferId)
					reject(new Error('Upload timeout - no completion received'))
				}, this.transferTimeout)
			}

			this.activeUploads.set(transferId, upload)

			// Stream all chunks without waiting for ACKs
			for (let chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
				const offset = chunkIndex * chunkSize
				const end = Math.min(offset + chunkSize, fileSize)
				const chunkData = data.slice(offset, end)

				const uploadData: FSUploadData = {
					transferId,
					chunkIndex,
					data: chunkData
				}

				// Send chunk as fire-and-forget message
				socket.emit(Messages.FSUploadData, uploadData)

				upload.chunksSent++

				// Report progress
				if (onProgress) {
					onProgress({
						transferId,
						bytesTransferred: end,
						totalBytes: fileSize,
						chunksCompleted: chunkIndex + 1,
						totalChunks,
						percentage: ((chunkIndex + 1) / totalChunks) * 100
					})
				}
			}

			// All chunks sent - now wait for completion message from server
			// The timeout will handle if server doesn't respond
		})
	}

	/**
	 * Cancel an ongoing transfer
	 */
	async cancelTransfer(transferId: string): Promise<{ success: boolean }> {
		const request: FSCancelTransfer = { transferId }

		// Clean up local state
		const download = this.activeDownloads.get(transferId)
		if (download) {
			clearTimeout(download.timeoutId)
			this.activeDownloads.delete(transferId)
			download.resolve({ success: false, error: 'Transfer cancelled' })
		}

		const upload = this.activeUploads.get(transferId)
		if (upload) {
			clearTimeout(upload.timeoutId)
			this.activeUploads.delete(transferId)
			upload.resolve({ success: false, error: 'Transfer cancelled' })
		}

		const response = await socket.request({
			fsCancelTransfer: request
		})

		if (response.fsCancelTransferResponse) {
			return { success: response.fsCancelTransferResponse.success }
		}

		return { success: false }
	}

	/**
	 * Helper: Upload a File object from browser
	 */
	async uploadFileFromBrowser(
		destinationPath: string,
		file: File,
		onProgress?: ProgressCallback
	): Promise<{ success: boolean; error?: string }> {
		const arrayBuffer = await file.arrayBuffer()
		const data = new Uint8Array(arrayBuffer)
		return this.uploadFile(destinationPath, data, onProgress)
	}

	/**
	 * Helper: Download a file and save it to browser
	 */
	async downloadFileAndSave(
		path: string,
		filename: string,
		onProgress?: ProgressCallback
	): Promise<{ success: boolean; error?: string }> {
		const result = await this.downloadFile(path, onProgress)

		if (!result.success || !result.data) {
			return { success: false, error: result.error }
		}

		// Create blob and trigger download
		const blob = new Blob([result.data.buffer as ArrayBuffer])
		const url = URL.createObjectURL(blob)
		const a = document.createElement('a')
		a.href = url
		a.download = filename
		document.body.appendChild(a)
		a.click()
		document.body.removeChild(a)
		URL.revokeObjectURL(url)

		return { success: true }
	}

	/**
	 * Cleanup listeners when no longer needed
	 */
	destroy() {
		this.downloadListenerCleanup?.()
		this.completeListenerCleanup?.()
		this.uploadCompleteListenerCleanup?.()

		// Cancel all active transfers
		for (const [, download] of this.activeDownloads) {
			clearTimeout(download.timeoutId)
			download.reject(new Error('FileSystemClient destroyed'))
		}
		this.activeDownloads.clear()

		for (const [, upload] of this.activeUploads) {
			clearTimeout(upload.timeoutId)
			upload.reject(new Error('FileSystemClient destroyed'))
		}
		this.activeUploads.clear()
	}
}

export const fileSystemClient = new FileSystemClient()
