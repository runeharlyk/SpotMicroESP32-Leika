import { afterEach, describe, expect, it, vi } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import { fakeRobot } from './fake-robot'
import { socket } from '../../src/lib/stores/socket'
import { FileSystemClient } from '../../src/lib/filesystem/chunkedTransfer'
import {
    FSDownloadComplete,
    FSDownloadData,
    FSDownloadMetadata
} from '../../src/lib/platform_shared/filesystem'
import {
    CoreDumpReport,
    FeaturesDataResponse,
    ModeData,
    ModesEnum
} from '../../src/lib/platform_shared/message'
import {
    coreDumpFilename,
    crashCause,
    crashFirmware,
    eraseCoreDump,
    readCoreDump
} from '../../src/lib/firmware/core-dump'
import LastCrash from '../../src/routes/system/status/LastCrash.svelte'
import { connectionFeatures } from '../../src/lib/stores/featureFlags'
import { mode } from '../../src/lib/stores/model-store'

const SHA = 'ab'.repeat(32)

const crash = (fields: Partial<CoreDumpReport> = {}) =>
    CoreDumpReport.create({
        partition: true,
        stored: true,
        readable: true,
        size: 9000,
        task: 'control',
        pc: 0x42001234,
        excCause: 28,
        excVaddr: 0x4,
        backtrace: [0x42001234, 0x42005678],
        appElfSha256: SHA,
        ...fields
    })

describe('crashCause', () => {
    it('prefers the abort or assert message the panic handler left', () => {
        expect(crashCause(crash({ panicReason: 'assert failed: tick motion.cpp:42' }))).toBe(
            'assert failed: tick motion.cpp:42'
        )
    })

    it('names an Xtensa memory fault with the address it faulted on', () => {
        expect(crashCause(crash())).toBe('LoadProhibited at 0x00000004')
    })

    it('leaves out EXCVADDR for a cause that does not set it', () => {
        expect(crashCause(crash({ excCause: 0, excVaddr: 0x3fc91234 }))).toBe('IllegalInstruction')
    })

    it('reads the cause with the RISC-V meanings on a RISC-V robot', () => {
        expect(crashCause(crash({ riscv: true, excCause: 5, excVaddr: 0x10 }))).toBe(
            'Load access fault at 0x00000010'
        )
        expect(crashCause(crash({ riscv: true, excCause: 2 }))).toBe('Illegal instruction')
    })

    it('shows the number of a cause it has no name for', () => {
        expect(crashCause(crash({ excCause: 10 }))).toBe('Exception cause 10')
        expect(crashCause(crash({ excCause: 99 }))).toBe('Exception cause 99')
    })
})

describe('crashFirmware', () => {
    it('tells the running firmware from another by the ELF hash', () => {
        expect(crashFirmware(crash(), SHA)).toBe('running')
        expect(crashFirmware(crash(), 'cd'.repeat(32))).toBe('other')
    })

    it('cannot tell while either hash is missing', () => {
        expect(crashFirmware(crash(), undefined)).toBe('unknown')
        expect(crashFirmware(crash({ appElfSha256: '' }), SHA)).toBe('unknown')
    })
})

it('names the downloaded file after the firmware that crashed', () => {
    expect(coreDumpFilename(crash())).toBe('coredump-abababab.bin')
    expect(coreDumpFilename(crash({ appElfSha256: '' }))).toBe('coredump.bin')
})

describe('core dump requests', () => {
    let robot: ReturnType<typeof fakeRobot> | undefined
    afterEach(() => robot?.restore())

    it('reads the report the robot sends', async () => {
        robot = fakeRobot(() => ({ coreDumpReport: crash() }))

        expect((await readCoreDump()).task).toBe('control')
        expect(robot.sent).toEqual(['coreDumpRequest'])
    })

    it('fails an erase with the robot reason', async () => {
        robot = fakeRobot(() => ({
            statusCode: 409,
            errorMessage: 'Deactivate first',
            coreDumpReport: crash()
        }))

        await expect(eraseCoreDump()).rejects.toThrow('Deactivate first')
    })
})

describe('FileSystemClient.downloadCoreDump', () => {
    const listeners = new Map<unknown, (data: unknown) => void>()
    let robot: ReturnType<typeof fakeRobot> | undefined

    afterEach(() => {
        robot?.restore()
        vi.restoreAllMocks()
        listeners.clear()
    })

    // A client whose inbound messages the test delivers, at a robot that answers the download with `stream`.
    function clientStreaming(stream: () => void) {
        vi.spyOn(socket, 'on').mockImplementation(((
            type: unknown,
            listener: (data: unknown) => void
        ) => {
            listeners.set(type, listener)
            return () => {}
        }) as typeof socket.on)
        robot = fakeRobot(name => {
            if (name === 'coreDumpDownload') setTimeout(stream)
            return { statusCode: 202 }
        })
        return new FileSystemClient()
    }

    const deliver = <T>(type: unknown, message: T) => listeners.get(type)!(message)

    it('assembles the chunks the robot streams into the whole dump', async () => {
        const dump = Uint8Array.from({ length: 2 ** 14 + 100 }, (_, i) => (i * 7) % 256)
        const client = clientStreaming(() => {
            deliver(
                FSDownloadMetadata,
                FSDownloadMetadata.create({
                    transferId: 4,
                    success: true,
                    fileSize: dump.length,
                    totalChunks: 2
                })
            )
            deliver(
                FSDownloadData,
                FSDownloadData.create({
                    transferId: 4,
                    chunkIndex: 0,
                    data: dump.subarray(0, 2 ** 14)
                })
            )
            deliver(
                FSDownloadData,
                FSDownloadData.create({
                    transferId: 4,
                    chunkIndex: 1,
                    data: dump.subarray(2 ** 14)
                })
            )
            deliver(
                FSDownloadComplete,
                FSDownloadComplete.create({
                    transferId: 4,
                    success: true,
                    totalChunks: 2,
                    fileSize: dump.length
                })
            )
        })
        const progress: number[] = []

        const result = await client.downloadCoreDump(p => progress.push(p.bytesTransferred))

        expect(robot!.sent).toEqual(['coreDumpDownload'])
        expect(result.success).toBe(true)
        expect(result.data).toEqual(dump)
        expect(progress).toEqual([2 ** 14, dump.length])
    })

    it('reports the robot refusal when no dump is stored', async () => {
        const client = clientStreaming(() =>
            deliver(
                FSDownloadMetadata,
                FSDownloadMetadata.create({ success: false, error: 'No core dump stored' })
            )
        )

        expect(await client.downloadCoreDump()).toEqual({
            success: false,
            error: 'No core dump stored'
        })
    })
})

describe('LastCrash', () => {
    let component: ReturnType<typeof mount> | undefined
    let robot: ReturnType<typeof fakeRobot> | undefined

    afterEach(() => {
        if (component) unmount(component)
        component = undefined
        robot?.restore()
        document.body.innerHTML = ''
    })

    const button = (label: string) =>
        [...document.body.querySelectorAll('button')].find(b => b.textContent?.trim() === label)

    async function open(report: CoreDumpReport, robotMode: ModesEnum, runningSha = SHA) {
        robot = fakeRobot(() => ({ coreDumpReport: report }))
        connectionFeatures.set(FeaturesDataResponse.create({ firmwareElfSha256: runningSha }))
        mode.set(ModeData.create({ mode: robotMode }))
        component = mount(LastCrash, { target: document.body })
        flushSync()
        await vi.waitFor(() => expect(document.body.textContent).not.toMatch('Loading...'))
        flushSync()
    }

    it('shows the summary of a stored crash with its backtrace', async () => {
        await open(crash({ backtraceCorrupted: true }), ModesEnum.DEACTIVATED)

        const text = document.body.textContent!
        expect(text).toMatch('LoadProhibited at 0x00000004')
        expect(text).toMatch('control')
        expect(text).toMatch('0x42001234 0x42005678 |<-CORRUPTED')
        expect(text).toMatch(SHA)
        expect(text).not.toMatch('another firmware')
        expect(button('Download')!.disabled).toBe(false)
        expect(button('Erase')!.disabled).toBe(false)
    })

    it('holds the erase back until the robot is deactivated', async () => {
        await open(crash(), ModesEnum.STAND)

        expect(button('Erase')!.disabled).toBe(true)
        mode.set(ModeData.create({ mode: ModesEnum.DEACTIVATED }))
        flushSync()
        expect(button('Erase')!.disabled).toBe(false)
    })

    it('warns when the crash came from another firmware', async () => {
        await open(crash(), ModesEnum.DEACTIVATED, 'cd'.repeat(32))

        expect(document.body.textContent).toMatch('another firmware')
    })

    it('offers neither button when no crash is stored', async () => {
        await open(CoreDumpReport.create({ partition: true }), ModesEnum.DEACTIVATED)

        expect(document.body.textContent).toMatch('No crash stored')
        expect(button('Download')).toBeUndefined()
        expect(button('Erase')).toBeUndefined()
    })

    it('still offers the download of a dump that fails its checksum', async () => {
        await open(crash({ readable: false }), ModesEnum.DEACTIVATED)

        expect(document.body.textContent).toMatch('fails its checksum')
        expect(button('Download')!.disabled).toBe(false)
    })
})
