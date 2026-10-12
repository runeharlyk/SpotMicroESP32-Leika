<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { Record, DownloadIcon } from '$lib/components/icons'
    import { recorder, LARGE_RECORDING_BYTES } from '$lib/recording/recorder'
    import { humanFileSize } from '$lib/utilities/string-utilities'
    import { saveFile } from '$lib/utilities/save-file'
    import { socket } from '$lib/stores/socket'

    let stoodStill = $state(false)

    const finished = $derived($recorder.finished)
    const stats = $derived(finished?.stats)
    const header = $derived(finished?.recording?.header)

    const counts: [key: string, label: string][] = [
        ['ticks', 'Ticks'],
        ['missing_ticks', 'Missing ticks'],
        ['dropped_ticks', 'Dropped ticks'],
        ['link_gaps', 'Link gaps'],
        ['reboots', 'Reboots']
    ]
    const timings: [key: string, label: string][] = [
        ['period', 'Tick period'],
        ['compute', 'Compute'],
        ['servo_write', 'Servo write'],
        ['imu_age', 'IMU age'],
        ['sensor_to_servo', 'Sensor to servo'],
        ['command_age', 'Command age']
    ]
    const stillness: [key: string, label: string, unit: string][] = [
        ['gyro_bias_x', 'Gyro bias x', 'rad/s'],
        ['gyro_bias_y', 'Gyro bias y', 'rad/s'],
        ['gyro_bias_z', 'Gyro bias z', 'rad/s'],
        ['gyro_noise_std', 'Gyro noise std', 'rad/s'],
        ['rpy_noise_deg', 'Roll/pitch noise', 'deg']
    ]
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <Record class="mr-2 h-6 w-6 shrink-0 self-end" />
    {/snippet}
    {#snippet title()}
        <span>Telemetry recording</span>
    {/snippet}

    <div class="flex flex-wrap items-center gap-2">
        {#if $recorder.recording}
            <button class="btn btn-error btn-sm" onclick={recorder.stop}>Stop</button>
            <span class="text-sm tabular-nums">
                {humanFileSize($recorder.bytes)}, {$recorder.frames} frames{$recorder.gaps ?
                    `, ${$recorder.gaps} link gaps`
                :   ''}
            </span>
        {:else}
            <button class="btn btn-primary btn-sm" disabled={!$socket} onclick={recorder.start}>
                Record
            </button>
        {/if}
        {#if finished && !$recorder.recording}
            <button
                class="btn btn-sm"
                onclick={() => saveFile(finished.blob, finished.filename)}
                aria-label="Download recording"
            >
                <DownloadIcon class="h-4 w-4" />
                {finished.filename} ({humanFileSize(finished.blob.size)})
            </button>
        {/if}
    </div>

    {#if $recorder.bytes >= LARGE_RECORDING_BYTES}
        <div role="alert" class="alert alert-warning alert-soft text-sm">
            The recording holds {humanFileSize($recorder.bytes)} in the browser; stop and download it
            before it grows further.
        </div>
    {/if}

    {#if finished?.error}
        <div role="alert" class="alert alert-error alert-soft text-sm">{finished.error}</div>
    {/if}

    {#if stats}
        {#if header}
            <p class="text-sm opacity-70">
                {header.variant}
                {header.firmwareVersion}, IMU {header.imuDriver} at {header.imuRateHz} Hz, compass
                {header.magRateHz} Hz
            </p>
        {/if}
        <table class="table-sm table w-full tabular-nums">
            <tbody>
                {#each counts as [key, label] (key)}
                    <tr><td>{label}</td><td class="text-right" colspan="2">{stats[key]}</td></tr>
                {/each}
                <tr class="opacity-70"
                    ><td>ms</td><td class="text-right">p50</td><td class="text-right">p99</td></tr
                >
                {#each timings as [key, label] (key)}
                    {#if stats[`${key}_ms_p50`] !== undefined}
                        <tr>
                            <td>{label}</td>
                            <td class="text-right">{stats[`${key}_ms_p50`].toFixed(2)}</td>
                            <td class="text-right">{stats[`${key}_ms_p99`].toFixed(2)}</td>
                        </tr>
                    {/if}
                {/each}
            </tbody>
        </table>
        <label class="label cursor-pointer gap-2 text-sm">
            <input type="checkbox" class="toggle toggle-sm" bind:checked={stoodStill} />
            The robot stood still throughout
        </label>
        {#if stoodStill}
            {#if stats.gyro_noise_std === undefined}
                <p class="text-sm opacity-70">The recording has no IMU samples.</p>
            {:else}
                <table class="table-sm table w-full tabular-nums">
                    <tbody>
                        {#each stillness as [key, label, unit] (key)}
                            <tr>
                                <td>{label}</td>
                                <td class="text-right">{stats[key].toFixed(4)} {unit}</td>
                            </tr>
                        {/each}
                    </tbody>
                </table>
            {/if}
        {/if}
    {/if}
</SettingsCard>
