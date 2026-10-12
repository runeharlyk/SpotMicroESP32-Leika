import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { fakeRobot } from './fake-robot'
import { socket } from '../../src/lib/stores/socket'
import { FeaturesDataResponse } from '../../src/lib/platform_shared/message'
import {
    OTA_CHUNK_SIZE,
    bootNewFirmware,
    downloadFirmwareOnRobot,
    sendFirmware
} from '../../src/lib/firmware/ota'
import { OtaDownloadState, OtaStatus } from '../../src/lib/platform_shared/message'

const imageOf = (size: number) => Uint8Array.from({ length: size }, (_, i) => i % 251)

describe('sendFirmware', () => {
    let robot: ReturnType<typeof fakeRobot> | undefined

    afterEach(() => robot?.restore())

    it('sends the whole image in order between the start and the finish', async () => {
        const image = imageOf(OTA_CHUNK_SIZE * 2 + 100)
        const chunks: { index: number; data: Uint8Array }[] = []
        let size = 0
        robot = fakeRobot((name, data) => {
            if (name === 'otaStart') size = data.otaStart!.size
            if (name === 'otaChunk') chunks.push(data.otaChunk!)
            return {}
        })
        const progress: number[] = []

        await sendFirmware(image, fraction => progress.push(fraction))

        expect(size).toBe(image.length)
        expect(robot.sent).toEqual(['otaStart', 'otaChunk', 'otaChunk', 'otaChunk', 'otaFinish'])
        expect(chunks.map(chunk => chunk.index)).toEqual([0, 1, 2])
        const received = new Uint8Array(chunks.flatMap(chunk => [...chunk.data]))
        expect(received).toEqual(image)
        expect(progress.at(-1)).toBe(1)
    })

    it('keeps at most four chunks waiting for the robot', async () => {
        const answers: (() => void)[] = []
        let waiting = 0
        let mostWaiting = 0
        robot = fakeRobot(name => {
            if (name !== 'otaChunk') return {}
            waiting++
            mostWaiting = Math.max(mostWaiting, waiting)
            return new Promise(resolve =>
                answers.push(() => {
                    waiting--
                    resolve({})
                })
            )
        })

        const sent = sendFirmware(imageOf(OTA_CHUNK_SIZE * 10), () => {})
        for (let answered = 0; answered < 10; answered++) {
            await vi.waitFor(() => expect(answers.length).toBeGreaterThan(0))
            answers.shift()!()
        }
        await sent

        expect(mostWaiting).toBe(4)
        expect(robot.sent.filter(name => name === 'otaChunk')).toHaveLength(10)
    })

    it('stops at the first refused chunk with the robot reason, and never finishes', async () => {
        robot = fakeRobot((name, data) =>
            name === 'otaChunk' && data.otaChunk!.index === 1 ?
                { statusCode: 500, errorMessage: 'Writing the update slot failed' }
            :   {}
        )

        await expect(sendFirmware(imageOf(OTA_CHUNK_SIZE * 12), () => {})).rejects.toThrow(
            'Writing the update slot failed'
        )

        const chunks = robot.sent.filter(name => name === 'otaChunk').length
        expect(chunks).toBeLessThan(12)
        expect(robot.sent).not.toContain('otaFinish')
    })

    it('sends nothing when the robot refuses to start', async () => {
        robot = fakeRobot(name =>
            name === 'otaStart' ? { statusCode: 409, errorMessage: 'Deactivate first' } : {}
        )

        await expect(sendFirmware(imageOf(100), () => {})).rejects.toThrow('Deactivate first')
        expect(robot.sent).toEqual(['otaStart'])
    })
})

describe('bootNewFirmware', () => {
    let robot: ReturnType<typeof fakeRobot> | undefined

    afterEach(() => {
        robot?.restore()
        vi.restoreAllMocks()
    })

    // The socket drops as the robot restarts; the restart request is what triggers it here.
    function closesOnRestart() {
        let closed: (() => void) | undefined
        vi.spyOn(socket, 'onEvent').mockImplementation((event, listener) => {
            if (event === 'close') closed = () => listener(undefined)
            return () => {}
        })
        return () => closed?.()
    }

    it('reports the new firmware running when the robot comes back with its hash', async () => {
        const close = closesOnRestart()
        robot = fakeRobot(name => {
            if (name === 'systemRestart') close()
            return {
                featuresDataResponse: FeaturesDataResponse.create({ firmwareElfSha256: 'new' })
            }
        })

        await expect(bootNewFirmware(hash => hash === 'new', { retryAfterMs: 0 })).resolves.toBe(
            'running'
        )
        expect(robot.sent).toEqual(['systemRestart', 'featuresDataRequest'])
    })

    it('reports a rollback when the robot comes back with another image', async () => {
        const close = closesOnRestart()
        robot = fakeRobot(name => {
            if (name === 'systemRestart') close()
            return {
                featuresDataResponse: FeaturesDataResponse.create({ firmwareElfSha256: 'old' })
            }
        })

        await expect(bootNewFirmware(hash => hash === 'new', { retryAfterMs: 0 })).resolves.toBe(
            'rolled back'
        )
    })

    it('asks again until the robot answers', async () => {
        const close = closesOnRestart()
        let asked = 0
        robot = fakeRobot(name => {
            if (name === 'systemRestart') close()
            if (name === 'featuresDataRequest' && ++asked < 3) throw new Error('Request timeout')
            return {
                featuresDataResponse: FeaturesDataResponse.create({ firmwareElfSha256: 'new' })
            }
        })

        await expect(bootNewFirmware(hash => hash === 'new', { retryAfterMs: 0 })).resolves.toBe(
            'running'
        )
        expect(asked).toBe(3)
    })

    it('gives up when the robot does not come back in time', async () => {
        const close = closesOnRestart()
        robot = fakeRobot(name => {
            if (name !== 'systemRestart') throw new Error('Request timeout')
            close()
            return {}
        })

        await expect(
            bootNewFirmware(hash => hash === 'new', { backWithinMs: 0, retryAfterMs: 0 })
        ).rejects.toThrow('did not come back')
    })
})

describe('downloadFirmwareOnRobot', () => {
    let robot: ReturnType<typeof fakeRobot> | undefined
    let report: ((status: OtaStatus) => void) | undefined
    let listening = false

    beforeEach(() => {
        listening = false
        vi.spyOn(socket, 'on').mockImplementation(((
            _type: unknown,
            listener: (s: OtaStatus) => void
        ) => {
            listening = true
            report = listener
            return () => {
                listening = false
            }
        }) as unknown as typeof socket.on)
    })

    afterEach(() => {
        robot?.restore()
        vi.restoreAllMocks()
        vi.useRealTimers()
    })

    const status = (state: OtaDownloadState, written: number, size: number, error = '') =>
        OtaStatus.create({ state, written, size, error })

    it('asks the robot for the URL and follows its progress until the image is in place', async () => {
        let asked: string | undefined
        robot = fakeRobot((name, data) => {
            if (name === 'otaDownload') asked = data.otaDownload!.url
            return { statusCode: 202 }
        })
        const progress: number[] = []
        const done = downloadFirmwareOnRobot('https://example.com/fw.bin', f => progress.push(f))
        await vi.waitFor(() => expect(asked).toBe('https://example.com/fw.bin'))
        report!(status(OtaDownloadState.OTA_DOWNLOADING, 0, 0))
        report!(status(OtaDownloadState.OTA_DOWNLOADING, 500, 1000))
        report!(status(OtaDownloadState.OTA_DOWNLOADED, 1000, 1000))
        await expect(done).resolves.toBeUndefined()
        expect(progress).toEqual([0.5, 1])
        expect(listening).toBe(false)
    })

    it('fails with the reason the robot gives, whether it refuses the request or the download fails', async () => {
        robot = fakeRobot(() => ({ statusCode: 409, errorMessage: 'Deactivate first' }))
        await expect(
            downloadFirmwareOnRobot('https://example.com/fw.bin', () => {})
        ).rejects.toThrow('Deactivate first')
        expect(listening).toBe(false)

        robot.restore()
        robot = fakeRobot(() => ({ statusCode: 202 }))
        const done = downloadFirmwareOnRobot('https://example.com/fw.bin', () => {})
        await vi.waitFor(() => expect(robot!.sent).toContain('otaDownload'))
        report!(status(OtaDownloadState.OTA_DOWNLOAD_FAILED, 300, 1000, 'The server answered 404'))
        await expect(done).rejects.toThrow('The server answered 404')
    })

    it('gives up when the robot stops reporting', async () => {
        vi.useFakeTimers()
        robot = fakeRobot(() => ({ statusCode: 202 }))
        const done = downloadFirmwareOnRobot('https://example.com/fw.bin', () => {}, {
            silentForMs: 1000
        })
        const failed = expect(done).rejects.toThrow('stopped reporting')
        await vi.advanceTimersByTimeAsync(900)
        report!(status(OtaDownloadState.OTA_DOWNLOADING, 100, 1000))
        await vi.advanceTimersByTimeAsync(900)
        expect(listening).toBe(true)
        await vi.advanceTimersByTimeAsync(200)
        await failed
    })
})
