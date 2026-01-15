import { socket } from '$lib/stores/socket'
import * as Messages from '$lib/platform_shared/message'
import type {
	FSDeleteRequest,
	FSMkdirRequest,
	FSListRequest,
	FSDownloadStartRequest,
	FSDownloadChunkRequest,
	FSUploadStartRequest,
	FSUploadChunkRequest,
	FSCancelTransferRequest,
	CorrelationResponse
} from '$lib/platform_shared/message'

const MAX_CHUNK_SIZE = 2**14 // ~= 16 kb

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

export class FileSystemClient {
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
	 * Download a file from the ESP32
	 */
	async downloadFile(
		path: string,
		onProgress?: ProgressCallback
	): Promise<{ success: boolean; data?: Uint8Array; error?: string }> {
		// Start download
		const startRequest: FSDownloadStartRequest = { path }

		const startResponse = await socket.request({
			fsDownloadStartRequest: startRequest
		})

		if (!startResponse.fsDownloadStartResponse) {
			return { success: false, error: 'Failed to start download' }
		}

		const startResp = startResponse.fsDownloadStartResponse

		if (!startResp.success) {
			return { success: false, error: startResp.error || 'Failed to start download' }
		}

		const transferId = startResp.transferId
		const totalChunks = startResp.totalChunks
		const fileSize = startResp.fileSize

		// Allocate buffer for entire file
		const buffer = new Uint8Array(fileSize)
		let offset = 0

		// Download chunks sequentially
		for (let chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
			const chunkRequest: FSDownloadChunkRequest = {
				transferId,
				chunkIndex
			}

			const chunkResponse = await socket.request({
				fsDownloadChunkRequest: chunkRequest
			})

			if (!chunkResponse.fsDownloadChunkResponse) {
				await this.cancelTransfer(transferId)
				return { success: false, error: `Failed to download chunk ${chunkIndex}` }
			}

			const chunkResp = chunkResponse.fsDownloadChunkResponse

			if (chunkResp.error) {
				await this.cancelTransfer(transferId)
				return { success: false, error: chunkResp.error }
			}

			// Copy chunk data to buffer
			if (chunkResp.data) {
				buffer.set(chunkResp.data, offset)
				offset += chunkResp.data.length
			}

			// Report progress
			if (onProgress) {
				onProgress({
					transferId,
					bytesTransferred: offset,
					totalBytes: fileSize,
					chunksCompleted: chunkIndex + 1,
					totalChunks,
					percentage: ((chunkIndex + 1) / totalChunks) * 100
				})
			}
		}

		return { success: true, data: buffer }
	}

	/**
	 * Upload a file to the ESP32
	 */
	async uploadFile(
		path: string,
		data: Uint8Array,
		onProgress?: ProgressCallback
	): Promise<{ success: boolean; error?: string }> {
		const fileSize = data.length
		const chunkSize = MAX_CHUNK_SIZE
		const totalChunks = Math.ceil(fileSize / chunkSize)

		// Start upload
		const startRequest: FSUploadStartRequest = {
			path,
			fileSize,
			chunkSize
		}

		const startResponse = await socket.request({
			fsUploadStartRequest: startRequest
		})

		if (!startResponse.fsUploadStartResponse) {
			return { success: false, error: 'Failed to start upload' }
		}

		const startResp = startResponse.fsUploadStartResponse

		if (!startResp.success) {
			return { success: false, error: startResp.error || 'Failed to start upload' }
		}

		const transferId = startResp.transferId
		const maxChunkSize = startResp.maxChunkSize || MAX_CHUNK_SIZE

		// Upload chunks sequentially
		for (let chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
			const offset = chunkIndex * chunkSize
			const end = Math.min(offset + chunkSize, fileSize)
			const chunkData = data.slice(offset, end)
			const isLast = chunkIndex === totalChunks - 1

			const chunkRequest: FSUploadChunkRequest = {
				transferId,
				chunkIndex,
				data: chunkData,
				isLast
			}

			const chunkResponse = await socket.request({
				fsUploadChunkRequest: chunkRequest
			})

			if (!chunkResponse.fsUploadChunkResponse) {
				await this.cancelTransfer(transferId)
				return { success: false, error: `Failed to upload chunk ${chunkIndex}` }
			}

			const chunkResp = chunkResponse.fsUploadChunkResponse

			if (!chunkResp.success) {
				await this.cancelTransfer(transferId)
				return { success: false, error: chunkResp.error || 'Failed to upload chunk' }
			}

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

		return { success: true }
	}

	/**
	 * Cancel an ongoing transfer
	 */
	async cancelTransfer(transferId: string): Promise<{ success: boolean }> {
		const request: FSCancelTransferRequest = { transferId }

		const response = await socket.request({
			fsCancelTransferRequest: request
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
		const blob = new Blob([result.data])
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
}

export const fileSystemClient = new FileSystemClient()
