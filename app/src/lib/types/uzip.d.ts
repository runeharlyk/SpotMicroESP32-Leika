declare module 'uzip' {
    interface UZIP {
        parse(data: Uint8Array | ArrayBuffer): Record<string, Uint8Array>
        compress(data: Record<string, Uint8Array>): Uint8Array | ArrayBuffer
        compressRaw(data: Uint8Array | ArrayBuffer): Uint8Array | ArrayBuffer
        decompress(data: Uint8Array | ArrayBuffer): Record<string, Uint8Array>
        decompressRaw(data: Uint8Array | ArrayBuffer): Uint8Array | ArrayBuffer
        encode(data: Record<string, Uint8Array>): Uint8Array | ArrayBuffer
        decode(data: Uint8Array | ArrayBuffer): Record<string, Uint8Array>
    }

    const uzip: UZIP
    export default uzip
}
