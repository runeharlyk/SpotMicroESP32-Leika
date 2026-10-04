/** What an ESP-IDF app image says about itself, read before it is sent to a robot. */
export interface FirmwareImage {
    bytes: Uint8Array
    size: number
    /** The build's version, as `git describe` named it. */
    version: string
    /** Hex SHA-256 of the image's ELF; the robot reports the same for the image it runs. */
    elfSha256: string
    /** The PlatformIO env it was built for; undefined for an image without the marker. */
    env: string | undefined
}

const IMAGE_MAGIC = 0xe9
// The image header (24 bytes) and the first segment's header (8) precede the app description.
const APP_DESCRIPTION = 32
const APP_DESCRIPTION_MAGIC = 0xabcd5432
const VERSION = APP_DESCRIPTION + 16
const ELF_SHA256 = APP_DESCRIPTION + 144
const APP_DESCRIPTION_END = ELF_SHA256 + 32
// The firmware keeps its build target behind this marker (esp32/src/features.cpp).
const ENV_MARKER = 'LEIKA_ENV='

export function readFirmwareImage(bytes: Uint8Array): FirmwareImage {
    if (bytes.length < APP_DESCRIPTION_END || bytes[0] !== IMAGE_MAGIC)
        throw new Error('This file is not an ESP32 app image')
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
    if (view.getUint32(APP_DESCRIPTION, true) !== APP_DESCRIPTION_MAGIC)
        throw new Error('This is a factory image or a bootloader; pick the <env>.bin app image')
    return {
        bytes,
        size: bytes.length,
        version: cString(bytes.subarray(VERSION, VERSION + 32)),
        elfSha256: Array.from(bytes.subarray(ELF_SHA256, APP_DESCRIPTION_END), byte =>
            byte.toString(16).padStart(2, '0')
        ).join(''),
        env: findEnv(bytes)
    }
}

function cString(bytes: Uint8Array) {
    const end = bytes.indexOf(0)
    return new TextDecoder('latin1').decode(end < 0 ? bytes : bytes.subarray(0, end))
}

function findEnv(bytes: Uint8Array) {
    const text = new TextDecoder('latin1').decode(bytes)
    const start = text.indexOf(ENV_MARKER)
    if (start < 0) return undefined
    const end = text.indexOf('\0', start)
    return text.slice(start + ENV_MARKER.length, end < 0 ? undefined : end)
}
