import { Result } from '$lib/utilities/result';

class FileService {
    private dbName = 'fileStorageDB';
    private dbVersion = 1;
    private storeName = 'files';
    private dbPromise: Promise<Result<IDBDatabase, string>>;

    constructor() {
        this.dbPromise = this.openDatabase();
    }

    private async openDatabase(): Promise<Result<IDBDatabase, string>> {
        return new Promise((resolve) => {
            const request = indexedDB.open(this.dbName, this.dbVersion);

            request.onerror = () => resolve(Result.err("Error opening database"));

            request.onsuccess = () => resolve(Result.ok(request.result));

            request.onupgradeneeded = (event) => {
                const db = request.result;
                if (!db.objectStoreNames.contains(this.storeName)) {
                    db.createObjectStore(this.storeName);
                }
            };
        });
    }

    private async getStore(mode: IDBTransactionMode): Promise<Result<IDBObjectStore, string>> {
        const dbResult = await this.dbPromise;
        if (dbResult.isErr()) {
            return Result.err("Database not initialized properly");
        }
        const db = dbResult.inner;
        const transaction = db.transaction(this.storeName, mode);
        return Result.ok(transaction.objectStore(this.storeName));
    }

    public async saveFile(key: string, file: Uint8Array): Promise<Result<IDBValidKey, string>> {
        const storeResult = await this.getStore("readwrite");
        if (storeResult.isErr()) {
            return Result.err("Failed to access object store for writing");
        }
        const store = storeResult.inner;

        return new Promise((resolve) => {
            const request = store.put(file, key);
            request.onsuccess = () => resolve(Result.ok(request.result));
            request.onerror = () => resolve(Result.err("Failed to save file"));
        });
    }

    public async getFile(key: string): Promise<Result<Uint8Array | undefined, string>> {
        const storeResult = await this.getStore("readonly");
        if (storeResult.isErr()) {
            return Result.err("Failed to access object store for reading");
        }
        const store = storeResult.inner;

        return new Promise((resolve) => {
            const request = store.get(key);
            
            request.onsuccess = () => 
                resolve(request.result ? Result.ok(request.result) : Result.err("File content not found")) 
            request.onerror = () => 
                resolve(Result.err("Failed to retrieve file"));
        });
    }
}

export default new FileService();