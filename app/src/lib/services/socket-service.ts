import { isConnected, socketData } from '$lib/stores';
import { Result, Ok } from '$lib/utilities';
import { resultService } from '$lib/services';
import { type WebSocketJsonMsg } from '$lib/models';

type WebsocketOutData = string | ArrayBufferLike | Blob | ArrayBufferView;

// TODO
/**
 * MOVE THE store to a store.ts file
 *
 * Make an object on the class that encapsulate all the stores
 *
 * Make the handle message function look up the type and set the value, to simplify the code
 */

class SocketService {
	private socket!: WebSocket;

	constructor() {}

	public connect(url: string): void {
		this.socket = new WebSocket(url);
		this.socket.binaryType = 'arraybuffer';
		this.socket.onopen = () => this.handleConnected();
		this.socket.onclose = () => this.handleDisconnected();
		this.socket.onmessage = (event: MessageEvent) =>
			resultService.handleResult(this.handleMessage(event), 'SocketService');
		this.socket.onerror = (error: Event) => console.log(error);
	}

	public send(data: WebsocketOutData): Result<void, string> {
		if (this.socket.readyState === WebSocket.OPEN) {
			this.socket.send(data);
			return Ok.void();
		}
		return Result.err('The connection is not open');
	}

	private handleConnected(): void {
		isConnected.set(true);
	}

	private handleDisconnected(): void {
		isConnected.set(false);
	}

	private getJsonFromMessage(msg: string): Result<WebSocketJsonMsg, string> {
		try {
			return Result.ok(JSON.parse(msg) as WebSocketJsonMsg);
		} catch (error) {
			return Result.err('Failed to parse socket message', error);
		}
	}

	private handleBufferMessage(buffer: ArrayBuffer): Result<void, string> {
		console.log(buffer);
		return Ok.void();
	}

	private handleMessage(event: MessageEvent): Result<void, string> {
		if (event.data instanceof ArrayBuffer) {
			return this.handleBufferMessage(event.data);
		}
		let msgRes = this.getJsonFromMessage(event.data);
		if (msgRes.isErr()) {
			return msgRes;
		}
		const msg = msgRes.inner;

		if (msg.type === 'log') {
			socketData.logs.update((entries) => {
				entries.push(msg.data);
				return entries;
			});
			return Ok.void();
		} else if (msg.data && msg.type in socketData) {
			socketData[msg.type].set(msg.data);
			return Ok.void();
		}

		return Result.err(`Got invalid msg: ${JSON.stringify(msg)}`);
	}
}

export default new SocketService();
