declare module 'uzip' {
	interface UZIP {
		parse(data: Uint8Array | ArrayBuffer): any;
		compress(data: any): Uint8Array | ArrayBuffer;
		compressRaw(data: Uint8Array | ArrayBuffer): Uint8Array | ArrayBuffer;
		decompress(data: Uint8Array | ArrayBuffer): any;
		decompressRaw(data: Uint8Array | ArrayBuffer): Uint8Array | ArrayBuffer;
		encode(data: any): Uint8Array | ArrayBuffer;
		decode(data: Uint8Array | ArrayBuffer): any;
	}

	const uzip: UZIP;
	export default uzip;
}
