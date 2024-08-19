import { Result } from '$lib/utilities/result';
import { browser } from '$app/environment';

class FileService {
	private dbPromise: Promise<Result<IDBDatabase, string>> | null = browser
		? this.openDatabase()
		: null;

	private async openDatabase(): Promise<Result<IDBDatabase, string>> {
		return new Promise((resolve) => {
			const request = indexedDB.open('fileStorageDB', 1);

			request.onupgradeneeded = () => {
				request.result.createObjectStore('files');
			};
			request.onsuccess = () => resolve(Result.ok(request.result));
			request.onerror = () => resolve(Result.err('Error opening database'));
		});
	}

	private async getStore(mode: IDBTransactionMode): Promise<Result<IDBObjectStore, string>> {
		if (!browser || !this.dbPromise)
			return Result.err('Not running in browser or DB not initialized');
		const dbResult = await this.dbPromise;
		if (dbResult.isErr()) return Result.err('Database not initialized');
		const store = dbResult.inner.transaction('files', mode).objectStore('files');
		return Result.ok(store);
	}

	public async saveFile(key: string, file: Uint8Array): Promise<Result<IDBValidKey, string>> {
		const storeResult = await this.getStore('readwrite');
		if (storeResult.isErr()) return Result.err('Failed to access store');

		return new Promise((resolve) => {
			const request = storeResult.inner.put(file, key);
			request.onsuccess = () => resolve(Result.ok(request.result));
			request.onerror = () => resolve(Result.err('Failed to save file'));
		});
	}

	public async getFile(key: string): Promise<Result<Uint8Array | undefined, string>> {
		const storeResult = await this.getStore('readonly');
		if (storeResult.isErr()) return Result.err('Failed to access store');

		return new Promise((resolve) => {
			const request = storeResult.inner.get(key);
			request.onsuccess = () =>
				resolve(request.result ? Result.ok(request.result) : Result.err('File not found'));
			request.onerror = () => resolve(Result.err('Failed to retrieve file'));
		});
	}
}

export default browser ? new FileService() : null;