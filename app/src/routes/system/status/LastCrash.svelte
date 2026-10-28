<script lang="ts">
    import { modals } from 'svelte-modals'
    import ConfirmDialog from '$lib/components/ConfirmDialog.svelte'
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import Spinner from '$lib/components/Spinner.svelte'
    import { Cancel, Delete, DownloadIcon, Warning } from '$lib/components/icons'
    import { connectionFeatures } from '$lib/stores/featureFlags'
    import { mode } from '$lib/stores/model-store'
    import { ModesEnum, type CoreDumpReport } from '$lib/platform_shared/message'
    import {
        crashCause,
        crashFirmware,
        eraseCoreDump,
        hex,
        readCoreDump,
        saveCoreDump
    } from '$lib/firmware/core-dump'

    let report: CoreDumpReport | null = $state(null)
    let loadError = $state('')
    let actionError = $state('')
    let downloaded: number | null = $state(null)
    let erasing = $state(false)

    const deactivated = $derived($mode.mode === ModesEnum.DEACTIVATED)
    const firmware = $derived(
        report ? crashFirmware(report, $connectionFeatures?.firmwareElfSha256) : 'unknown'
    )

    readCoreDump().then(
        read => (report = read),
        error => (loadError = (error as Error).message)
    )

    async function download() {
        if (!report) return
        actionError = ''
        downloaded = 0
        try {
            await saveCoreDump(report, fraction => (downloaded = fraction))
        } catch (error) {
            actionError = (error as Error).message
        } finally {
            downloaded = null
        }
    }

    function confirmErase() {
        modals.open(ConfirmDialog, {
            title: 'Erase the core dump',
            message: 'The crash can no longer be decoded once its dump is erased.',
            labels: {
                cancel: { label: 'Abort', icon: Cancel },
                confirm: { label: 'Erase', icon: Delete }
            },
            onConfirm: async () => {
                modals.close()
                actionError = ''
                erasing = true
                try {
                    report = await eraseCoreDump()
                } catch (error) {
                    actionError = (error as Error).message
                } finally {
                    erasing = false
                }
            }
        })
    }
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <Warning class="mr-2 h-6 w-6 shrink-0 self-end" />
    {/snippet}
    {#snippet title()}
        <span>Last Crash</span>
    {/snippet}

    {#if loadError}
        <div class="alert alert-error" role="alert">Could not read the core dump: {loadError}</div>
    {:else if !report}
        <Spinner />
    {:else if !report.partition}
        <p>This robot's partition table has no coredump partition, so a crash leaves no dump.</p>
    {:else if !report.stored}
        <p>No crash stored.</p>
    {:else}
        <div class="flex flex-col space-y-2">
            {#if report.readable}
                <dl class="grid grid-cols-[max-content_1fr] gap-x-4 gap-y-1 text-sm">
                    <dt class="font-bold">Cause</dt>
                    <dd>{crashCause(report)}</dd>
                    <dt class="font-bold">Task</dt>
                    <dd>{report.task}</dd>
                    <dt class="font-bold">PC</dt>
                    <dd class="font-mono">{hex(report.pc)}</dd>
                    {#if report.backtrace.length}
                        <dt class="font-bold">Backtrace</dt>
                        <dd class="font-mono break-all">
                            {report.backtrace.map(hex).join(' ')}{report.backtraceCorrupted ?
                                ' |<-CORRUPTED'
                            :   ''}
                        </dd>
                    {/if}
                    <dt class="font-bold">Firmware</dt>
                    <dd class="font-mono break-all">{report.appElfSha256}</dd>
                </dl>
                {#if firmware === 'other'}
                    <div class="alert alert-warning" role="alert">
                        The crash came from another firmware than the one running now; decode it
                        with that firmware's ELF.
                    </div>
                {/if}
            {:else}
                <div class="alert alert-warning" role="alert">
                    A dump of {report.size} bytes is stored, but it fails its checksum, so it has no summary.
                </div>
            {/if}
            <p class="text-sm opacity-75">
                Decode the downloaded dump with <code>esp-coredump info_corefile</code> and the firmware's
                ELF; see docs/websocket.md.
            </p>
        </div>
    {/if}

    {#if downloaded !== null}
        <progress class="progress progress-primary mt-2 w-full" value={downloaded} max="1"
        ></progress>
    {/if}
    {#if actionError}
        <div class="alert alert-error mt-2" role="alert">{actionError}</div>
    {/if}

    {#if report?.stored}
        <div class="mt-4 flex flex-wrap justify-end gap-2">
            <button
                class="btn btn-primary inline-flex items-center"
                disabled={downloaded !== null || erasing}
                onclick={download}
            >
                <DownloadIcon class="mr-2 h-5 w-5" />
                <span>Download</span>
            </button>
            <button
                class="btn btn-secondary inline-flex items-center"
                disabled={!deactivated || downloaded !== null || erasing}
                title={deactivated ? undefined : 'Deactivate the robot first'}
                onclick={confirmErase}
            >
                <Delete class="mr-2 h-5 w-5" />
                <span>Erase</span>
            </button>
        </div>
    {/if}
</SettingsCard>
