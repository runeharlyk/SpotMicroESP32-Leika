import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import path from 'node:path'
import { readFirmwareImage } from '../../src/lib/firmware/image'

// The image header and app description of a real esp32-wroom-camera build; the rest of the image is left out.
const head = new Uint8Array(readFileSync(path.join(__dirname, 'fixtures', 'firmware-head.bin')))

function image(...parts: (Uint8Array | string)[]) {
    const bytes = parts.map(part =>
        typeof part === 'string' ? new TextEncoder().encode(part) : part
    )
    const joined = new Uint8Array(bytes.reduce((size, part) => size + part.length, 0))
    let offset = 0
    for (const part of bytes) {
        joined.set(part, offset)
        offset += part.length
    }
    return joined
}

describe('readFirmwareImage', () => {
    it('reads the version and the ELF hash from the app description', () => {
        const read = readFirmwareImage(image(head, '\0LEIKA_ENV=esp32-wroom-camera\0'))
        expect(read.version).toBe('v0.3.0-4-g8f158dd-dirty')
        expect(read.elfSha256).toBe(
            '3768e56bc7bc215092ba688e93b4e8040b8df049e15fa8b99695389886e0a3b4'
        )
        expect(read.size).toBe(head.length + 30)
    })

    it('reads the env the image was built for from its marker', () => {
        const read = readFirmwareImage(image(head, 'padding\0LEIKA_ENV=seeed-xiao-esp32s3\0more'))
        expect(read.env).toBe('seeed-xiao-esp32s3')
    })

    it('reports no env for an image without the marker', () => {
        expect(readFirmwareImage(head).env).toBeUndefined()
    })

    it('refuses a file that is not a firmware image', () => {
        const text = image('not firmware'.repeat(40))
        expect(() => readFirmwareImage(text)).toThrow('not an ESP32 app image')
    })

    it('refuses a factory image, which starts with the bootloader', () => {
        const factory = head.slice()
        // A bootloader description stands where an app's would, with its own magic byte.
        factory.set([0x50, 0, 0, 0], 32)
        expect(() => readFirmwareImage(factory)).toThrow('factory image')
    })

    it('refuses a file too short to hold an app description', () => {
        expect(() => readFirmwareImage(head.subarray(0, 100))).toThrow('not an ESP32 app image')
    })
})
