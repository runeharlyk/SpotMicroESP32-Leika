import { socket } from '$lib/stores/socket'
import * as FSMessages from '$lib/platform_shared/filesystem'
import type {
	FSDeleteRequest,
	FSMkdirRequest,
	FSListRequest,
	FSDownloadRequest,
	FSDownloadMetadata,
	FSDownloadData,
	FSDownloadComplete,
	FSUploadStart,
	FSUploadData,
	FSUploadComplete,
	FSCancelTransfer
} from '$lib/platform_shared/filesystem'
import type { Result, DataResult, ListResult, ProgressCallback } from '$lib/types/models'

const MAX_CHUNK_SIZE = 1024 * 64 // 64KB - must match ESP32 FS_MAX_CHUNK_SIZE

type TimeoutId = ReturnType<typeof setTimeout>
type CleanupFn = (() => void) | null

interface TransferBase<T extends Result> {
	resolve: (result: T) => void
	reject: (error: Error) => void
	onProgress?: ProgressCallback
	timeoutId: TimeoutId
}

interface ActiveDownload extends TransferBase<DataResult> {
	path: string
	buffer: Uint8Array
	fileSize: number
	totalChunks: number
	chunksReceived: number
	bytesReceived: number
}

interface ActiveUpload extends TransferBase<Result> {
	path: string
	transferId: number
	totalChunks: number
	chunksSent: number
}

export class FileSystemClient {
	private activeDownloads = new Map<number, ActiveDownload>()
	private activeUploads = new Map<number, ActiveUpload>()
	private pendingDownloads = new Map<string, TransferBase<DataResult>>()
	private metadataListenerCleanup: CleanupFn = null
	private downloadListenerCleanup: CleanupFn = null
	private completeListenerCleanup: CleanupFn = null
	private uploadCompleteListenerCleanup: CleanupFn = null
	private transferTimeout = 300000

	constructor() {
		this.setupListeners()
	}

	private setupListeners() {
		// Listen for download metadata (sent first with file size)
		this.metadataListenerCleanup = socket.on(
			FSMessages.FSDownloadMetadata,
			(metadata: FSDownloadMetadata) => {
				this.handleDownloadMetadata(metadata)
			}
		)

		// Listen for download data chunks
		this.downloadListenerCleanup = socket.on(
			FSMessages.FSDownloadData,
			(data: FSDownloadData) => {
				this.handleDownloadData(data)
			}
		)

		// Listen for download completion
		this.completeListenerCleanup = socket.on(
			FSMessages.FSDownloadComplete,
			(complete: FSDownloadComplete) => {
				this.handleDownloadComplete(complete)
			}
		)

		// Listen for upload completion
		this.uploadCompleteListenerCleanup = socket.on(
			FSMessages.FSUploadComplete,
			(complete: FSUploadComplete) => {
				this.handleUploadComplete(complete)
			}
		)
	}

	private handleDownloadMetadata(metadata: FSDownloadMetadata) {
		// Find the pending download by path (we don't have transferId yet)
		// The metadata arrives in response to a download request
		const pending = this.pendingDownloads.values().next().value
		if (!pending) {
			console.warn(`Received download metadata but no pending download`)
			return
		}

		// Clear initial timeout
		clearTimeout(pending.timeoutId)

		// Get the path from the pending downloads (first one)
		const [path] = this.pendingDownloads.keys()
		this.pendingDownloads.delete(path)

		if (!metadata.success) {
			pending.resolve({ success: false, error: metadata.error || 'Download failed' })
			return
		}

		const transferId = metadata.transferId

		// Now we know the exact file size - allocate properly sized buffer
		const buffer = new Uint8Array(metadata.fileSize)

		const download: ActiveDownload = {
			path,
			buffer,
			fileSize: metadata.fileSize,
			totalChunks: metadata.totalChunks,
			chunksReceived: 0,
			bytesReceived: 0,
			resolve: pending.resolve,
			reject: pending.reject,
			onProgress: pending.onProgress,
			timeoutId: setTimeout(() => {
				this.activeDownloads.delete(transferId)
				pending.reject(new Error('Download timeout'))
			}, this.transferTimeout)
		}

		this.activeDownloads.set(transferId, download)
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

	/** Delete a file or directory on the ESP32 */
	async deleteFile(path: string): Promise<Result> {
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

	/** Create a directory on the ESP32 */
	async createDirectory(path: string): Promise<Result> {
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

	/** List files and directories at the given path */
	async listDirectory(path = '/'): Promise<ListResult> {
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

	/** Download a file from the ESP32 using streaming transfer */
	async downloadFile(path: string, onProgress?: ProgressCallback): Promise<DataResult> {
		return new Promise((resolve, reject) => {
			// Send download request - server will send metadata first, then stream chunks
			const request: FSDownloadRequest = { path }

			// Set up timeout for initial metadata response
			const initialTimeout = setTimeout(() => {
				this.pendingDownloads.delete(path)
				reject(new Error('Download request timeout - no metadata received'))
			}, this.transferTimeout)

			// Track this pending download - will be converted to active when metadata arrives
			this.pendingDownloads.set(path, {
				resolve,
				reject,
				onProgress,
				timeoutId: initialTimeout
			})

			// Send the download request (server will respond with metadata, then stream data)
			socket.request({ fsDownloadRequest: request }).catch((err) => {
				clearTimeout(initialTimeout)
				this.pendingDownloads.delete(path)
				reject(err)
			})
		})
	}

	/** Upload a file to the ESP32 using streaming transfer */
	async uploadFile(path: string, data: Uint8Array, onProgress?: ProgressCallback): Promise<Result> {
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
				socket.emit(FSMessages.FSUploadData, uploadData)

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

	/** Cancel an ongoing transfer */
	async cancelTransfer(transferId: number): Promise<Pick<Result, 'success'>> {
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

	/** Upload a File object from browser */
	async uploadFileFromBrowser(
		destinationPath: string,
		file: File,
		onProgress?: ProgressCallback
	): Promise<Result> {
		const arrayBuffer = await file.arrayBuffer()
		const data = new Uint8Array(arrayBuffer)
		return this.uploadFile(destinationPath, data, onProgress)
	}

	/** Download a file and save it to browser */
	async downloadFileAndSave(
		path: string,
		filename: string,
		onProgress?: ProgressCallback
	): Promise<Result> {
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

	/** Cleanup listeners when no longer needed */
	destroy() {
		this.metadataListenerCleanup?.()
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
