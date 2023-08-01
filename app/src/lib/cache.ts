class FileCache {
    private request: IDBOpenDBRequest;
    private db: IDBDatabase | null = null;
    private store: IDBObjectStore | null = null;

    dbName = 'fileStorageDB';
    dbVersion = 1;
    storeName = 'files';
    constructor() {
        this.request = indexedDB.open(this.dbName, this.dbVersion);
        this.request.onerror = (event) => {
            console.error("An error occurred with IndexedDB", event);
        };

        this.request.onupgradeneeded = () => {
            this.db = this.request.result;
            this.store = this.db.createObjectStore(this.storeName);
        };

        this.request.onsuccess = () => {
            this.db = this.request.result;
            const transaction = this.db.transaction(this.storeName, "readwrite");
            this.store = transaction.objectStore(this.storeName);
        }
    }

    public isOpen(): boolean {
        return this.db !== null;
    }

    public async saveFile(key:string, file: Uint8Array): Promise<string | IDBValidKey> {
        return new Promise((resolve, reject) => {
            if(!this.db) {
                reject("Database not open")
                return;
            }
            const transaction = this.db.transaction(this.storeName, "readwrite");
            const store = transaction.objectStore(this.storeName);
            const request = store.put(file, key);
            if(!request) {
                reject("Request not created")
                return 
            }
            request.onsuccess = () => {                
                resolve(request.result);
            };
            request.onerror = () => {
                reject(request.error);
            };
        });
    }

    public async getFile(key:string): Promise<Uint8Array | null> {
        return new Promise((resolve, reject) => {
            if(!key) {
                reject("Key was not defined")
                return;
            }
            if(!this.db) {
                reject("Database not open")
                return;
            }
            const transaction = this.db.transaction(this.storeName, "readwrite");
            const store = transaction.objectStore(this.storeName);
            
            const request = store.get(key);
            if(!request) {
                reject("Request not created")
                return 
            }
            request.onsuccess = () => {                
                resolve(request.result);
            };
            request.onerror = () => {
                reject(request.error);
            };
        });
    }

    public async openDatabase(): Promise<IDBDatabase> {
        return new Promise((resolve, reject) => {
          const request = indexedDB.open(this.dbName, this.dbVersion);
      
          request.onerror = (event) => {
            reject('Error opening database');
          };
      
          request.onsuccess = (event) => {
            const db = event.target?.result;
            this.db = db;
            resolve(db);
          };
      
          request.onupgradeneeded = (event) => {
            this.db = event.target?.result;
            this.db?.createObjectStore('files', { autoIncrement: true });
          };
        });
    }
}

export default new FileCache();