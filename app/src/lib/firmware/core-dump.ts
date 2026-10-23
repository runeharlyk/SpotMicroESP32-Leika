import { robotRequest } from '$lib/robot-request'
import { fileSystemClient } from '$lib/filesystem/chunkedTransfer'
import { saveFile } from '$lib/utilities/save-file'
import type { CoreDumpReport } from '$lib/platform_shared/message'

/** What the robot kept from its last crash. */
export async function readCoreDump(): Promise<CoreDumpReport> {
    const reply = await robotRequest({ coreDumpRequest: {} })
    if (!reply.coreDumpReport) throw new Error('The robot sent no core dump report')
    return reply.coreDumpReport
}

/** Erases the stored dump; the robot refuses unless it is deactivated. Resolves to the report after the erase. */
export async function eraseCoreDump(): Promise<CoreDumpReport> {
    const reply = await robotRequest({ coreDumpErase: {} })
    if (!reply.coreDumpReport) throw new Error('The robot sent no core dump report')
    return reply.coreDumpReport
}

/** Streams the dump from the robot and hands it to the browser as a file named after the firmware that crashed. */
export async function saveCoreDump(
    report: CoreDumpReport,
    onProgress?: (fraction: number) => void
) {
    const result = await fileSystemClient.downloadCoreDump(progress =>
        onProgress?.(progress.bytesTransferred / progress.totalBytes)
    )
    if (!result.success || !result.data) throw new Error(result.error || 'Download failed')
    saveFile(result.data.buffer as ArrayBuffer, coreDumpFilename(report))
}

export function coreDumpFilename(report: CoreDumpReport): string {
    const firmware = report.appElfSha256.slice(0, 8)
    return firmware ? `coredump-${firmware}.bin` : 'coredump.bin'
}

// The panic handler's names, indexed by cause (esp_system/port/arch/*/panic_arch.c in ESP-IDF 5.5).
// prettier-ignore
const XTENSA_CAUSES = [
    'IllegalInstruction', 'Syscall', 'InstructionFetchError', 'LoadStoreError',
    'Level1Interrupt', 'Alloca', 'IntegerDivideByZero', 'PCValue',
    'Privileged', 'LoadStoreAlignment', '', '',
    'InstrPDAddrError', 'LoadStorePIFDataError', 'InstrPIFAddrError', 'LoadStorePIFAddrError',
    'InstTLBMiss', 'InstTLBMultiHit', 'InstFetchPrivilege', '',
    'InstrFetchProhibited', '', '', '',
    'LoadStoreTLBMiss', 'LoadStoreTLBMultihit', 'LoadStorePrivilege', '',
    'LoadProhibited', 'StoreProhibited', '', '',
    'Cp0Dis', 'Cp1Dis', 'Cp2Dis', 'Cp3Dis',
    'Cp4Dis', 'Cp5Dis', 'Cp6Dis', 'Cp7Dis'
]
// prettier-ignore
const RISCV_CAUSES = [
    'Instruction address misaligned', 'Instruction access fault', 'Illegal instruction', 'Breakpoint',
    'Load address misaligned', 'Load access fault', 'Store address misaligned', 'Store access fault',
    'Environment call from U-mode', 'Environment call from S-mode', '', 'Environment call from M-mode',
    'Instruction page fault', 'Load page fault', '', 'Store page fault'
]

// The causes whose EXCVADDR or mtval holds the address that faulted; for the others it holds nothing of this crash.
const XTENSA_ADDRESS_FAULTS = new Set([3, 9, 20, 28, 29])
const RISCV_ADDRESS_FAULTS = new Set([0, 1, 4, 5, 6, 7])

export const hex = (value: number) => `0x${value.toString(16).padStart(8, '0')}`

/**
 * Why the robot crashed, in the panic handler's words: the abort or assert message when there is one, else the
 * exception's name, with the address it faulted on when the cause is a memory fault.
 */
export function crashCause(report: CoreDumpReport): string {
    if (report.panicReason) return report.panicReason
    const name = (report.riscv ? RISCV_CAUSES : XTENSA_CAUSES)[report.excCause]
    if (!name) return `Exception cause ${report.excCause}`
    const addressFault = (report.riscv ? RISCV_ADDRESS_FAULTS : XTENSA_ADDRESS_FAULTS).has(
        report.excCause
    )
    return addressFault ? `${name} at ${hex(report.excVaddr)}` : name
}

export type CrashFirmware = 'running' | 'other' | 'unknown'

/**
 * Whether the dump came from the firmware the robot runs now, which decoding it needs the ELF of. The dump carries the
 * hash's first CONFIG_APP_RETRIEVE_LEN_ELF_SHA hex digits, 9 by default, and the robot all 64 of the running one's.
 */
export function crashFirmware(
    report: CoreDumpReport,
    runningElfSha256: string | undefined
): CrashFirmware {
    if (!report.appElfSha256 || !runningElfSha256) return 'unknown'
    return runningElfSha256.startsWith(report.appElfSha256) ? 'running' : 'other'
}
