import { Result, Ok } from '$lib/utilities';
import { writable, type Writable } from 'svelte/store';

type WebsocketData = string | ArrayBufferLike | Blob | ArrayBufferView

class SocketService {

    public isConnected = writable(false);
    public angles = writable(new Int16Array(12).fill(0));
    public log = writable([] as string[]);
    public battery = writable({});
    public mpu = writable({ heading: 0 });
    public distances = writable({});
    public settings = writable({});
    public systemInfo = writable({} as number);
    public dataBuffer = writable(new Float32Array(13));
    public servoBuffer: Writable<Int16Array | number[]> = writable(new Int16Array(12));
    public data = writable();
    private socket!: WebSocket;

    constructor() {}

    public connect(url: string): void {
        this.socket = new WebSocket(url);
        this.socket.binaryType = "arraybuffer";
        this.socket.onopen = () => this.handleConnected();
        this.socket.onclose = () => this.handleDisconnected();
        this.socket.onmessage = (event: unknown) => this.handleMessage(event);
    }

    public send(data: WebsocketData): Result<void, string> {
        if (this.socket.readyState === WebSocket.OPEN){
            this.socket.send(data)
            return Ok.void()
        }
        return Result.err("The connection is not open")
    }

    private handleConnected(): void {
        this.isConnected.set(true);
    }

    private handleDisconnected(): void {
        this.isConnected.set(false);
    }

    private handleMessage(event: any): void {
        if (event.data instanceof ArrayBuffer) {
            let buffer = new Int8Array(event.data);
            if (buffer.length === 44) {
                this.dataBuffer.set(new Float32Array(buffer.buffer));
            }
        } else {
            let data = event.data;
            try {
                data = JSON.parse(event.data);
            } catch (error) {
                console.warn(error);
            }
            switch (data.type) {
                case "angles":
                    this.angles.set(data.angles);
                    break;
                case "logs":
                    this.log.set(data.logs);
                    break;
                case "log":
                    this.log.update(entries => { entries.push(data.log); return entries; });
                    break;
                case "settings":
                    this.settings.set(data.settings);
                case "info":
                    this.systemInfo.set(data.info);
                    break;
                case "mpu":
                    this.mpu.set(data.mpu);
                    break;
                case "distances":
                    this.distances.set(data.distances);
                    break;
                case "battery":
                    this.battery.set(data.battery);
                    break;
            }
        }
    }
}

export default new SocketService();