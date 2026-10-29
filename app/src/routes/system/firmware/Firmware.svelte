<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { Chip, UploadIcon, DownloadIcon } from '$lib/components/icons'
    import { connectionFeatures } from '$lib/stores/featureFlags'
    import { mode } from '$lib/stores/model-store'
    import { ModeData, ModesEnum } from '$lib/platform_shared/message'
    import { readFirmwareImage, type FirmwareImage } from '$lib/firmware/image'
    import { latestRelease } from '$lib/firmware/release'
    import {
        bootNewFirmware,
        downloadFirmwareOnRobot,
        sendFirmware,
        type BootOutcome
    } from '$lib/firmware/ota'
    import { wifiState } from '$lib/robot/state'

    type Step =
        | { kind: 'idle' }
        | { kind: 'busy'; doing: string }
        | { kind: 'chosen'; image: FirmwareImage; source: string }
        | { kind: 'sending'; fraction: number }
        | { kind: 'downloading'; tag: string; fraction: number }
        | { kind: 'restarting' }
        | { kind: 'done'; outcome: BootOutcome }
        | { kind: 'failed'; message: string }

    let step: Step = $state({ kind: 'idle' })
    let fileInput: HTMLInputElement | undefined = $state()

    const robotEnv = $derived($connectionFeatures?.firmwareBuiltTarget)
    const deactivated = $derived($mode.mode === ModesEnum.DEACTIVATED)
    const working = $derived(['busy', 'sending', 'downloading', 'restarting'].includes(step.kind))
    // A station with an address: the robot can reach the release site itself (WifiStatus 3 is connected).
    const robotOnline = $derived(!$wifiState.stale && $wifiState.value?.status === 3)
    // Builds whose app slot has no room for TLS (esp32-camera, esp32dev) cannot download their own update.
    const canDownload = $derived($connectionFeatures?.otaDownload ?? false)

    function choose(bytes: Uint8Array, source: string) {
        try {
            step = { kind: 'chosen', image: readFirmwareImage(bytes), source }
        } catch (error) {
            step = { kind: 'failed', message: (error as Error).message }
        }
    }

    async function pickFile(event: Event) {
        const file = (event.currentTarget as HTMLInputElement).files?.[0]
        if (!file) return
        choose(new Uint8Array(await file.arrayBuffer()), file.name)
        fileInput!.value = ''
    }

    async function fetchRelease() {
        if (!robotEnv) return
        step = { kind: 'busy', doing: 'Looking for the latest release' }
        try {
            const release = await latestRelease(robotEnv)
            if (!release) {
                step = {
                    kind: 'failed',
                    message: `The latest release has no image for ${robotEnv}`
                }
                return
            }
            step = { kind: 'busy', doing: `Downloading ${release.tag}` }
            choose(await release.download(), `release ${release.tag}`)
        } catch (error) {
            step = { kind: 'failed', message: (error as Error).message }
        }
    }

    async function update(image: FirmwareImage) {
        step = { kind: 'sending', fraction: 0 }
        try {
            await sendFirmware(image.bytes, fraction => (step = { kind: 'sending', fraction }))
            step = { kind: 'restarting' }
            const outcome = await bootNewFirmware(hash => hash === image.elfSha256)
            step = { kind: 'done', outcome }
        } catch (error) {
            step = { kind: 'failed', message: (error as Error).message }
        }
    }

    // The robot downloads the image itself, over its own WiFi, rather than the app sending it over this link. The app
    // never sees the image's hash, so the robot runs it if it comes back with another hash than it had.
    async function installOnRobot() {
        if (!robotEnv) return
        step = { kind: 'busy', doing: 'Looking for the latest release' }
        try {
            const release = await latestRelease(robotEnv)
            if (!release) {
                step = {
                    kind: 'failed',
                    message: `The latest release has no image for ${robotEnv}`
                }
                return
            }
            const running = $connectionFeatures?.firmwareElfSha256
            const tag = release.tag
            step = { kind: 'downloading', tag, fraction: 0 }
            await downloadFirmwareOnRobot(
                release.url,
                fraction => (step = { kind: 'downloading', tag, fraction })
            )
            step = { kind: 'restarting' }
            const outcome = await bootNewFirmware(hash => hash !== running)
            step = { kind: 'done', outcome }
        } catch (error) {
            step = { kind: 'failed', message: (error as Error).message }
        }
    }

    const deactivate = () => mode.set(ModeData.create({ mode: ModesEnum.DEACTIVATED }))
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <Chip class="mr-2 h-6 w-6 shrink-0 self-end" />
    {/snippet}
    {#snippet title()}
        <span>Firmware Update</span>
    {/snippet}

    <div class="flex flex-col gap-4">
        <p>
            Running <strong>{$connectionFeatures?.firmwareVersion ?? 'unknown'}</strong>
            built for <strong>{robotEnv ?? 'an unknown board'}</strong>.
        </p>

        {#if !deactivated}
            <div class="alert alert-warning" role="alert">
                <span>The robot updates only while deactivated.</span>
                <button class="btn btn-sm" onclick={deactivate}>Deactivate</button>
            </div>
        {/if}

        <div class="flex flex-wrap gap-2">
            <button class="btn btn-primary" disabled={working} onclick={() => fileInput!.click()}>
                <UploadIcon class="mr-2 h-5 w-5" />Choose a .bin file
            </button>
            <button class="btn" disabled={working || !robotEnv} onclick={fetchRelease}>
                <DownloadIcon class="mr-2 h-5 w-5" />Latest release
            </button>
            {#if canDownload && robotOnline}
                <button
                    class="btn"
                    disabled={working || !robotEnv || !deactivated}
                    onclick={installOnRobot}
                >
                    <DownloadIcon class="mr-2 h-5 w-5" />Install the latest release on the robot
                </button>
            {/if}
            <input
                type="file"
                accept=".bin"
                class="hidden"
                bind:this={fileInput}
                onchange={pickFile}
            />
        </div>

        {#if step.kind === 'busy'}
            <p>{step.doing}...</p>
        {:else if step.kind === 'chosen'}
            {@const image = step.image}
            <div class="flex flex-col gap-2">
                <p>
                    {step.source}: version <strong>{image.version}</strong>, {(
                        image.size / 1024
                    ).toFixed(0)} KB, built for <strong>{image.env ?? 'an unknown board'}</strong>.
                </p>
                {#if image.env !== robotEnv}
                    <div class="alert alert-warning" role="alert">
                        {image.env ?
                            `This image is for ${image.env}, not for this robot's ${robotEnv}.`
                        :   'This image does not say which board it is for.'}
                        A wrong board's pins may leave the robot without its servos or sensors.
                    </div>
                {/if}
                <div>
                    <button
                        class="btn btn-primary"
                        disabled={!deactivated}
                        onclick={() => update(image)}
                    >
                        {image.env === robotEnv ? 'Update' : 'Update anyway'}
                    </button>
                </div>
            </div>
        {:else if step.kind === 'sending'}
            <div class="flex flex-col gap-1">
                <span>Sending the image: {(step.fraction * 100).toFixed(0)} %</span>
                <progress class="progress progress-primary w-full" value={step.fraction} max="1"
                ></progress>
            </div>
        {:else if step.kind === 'downloading'}
            <div class="flex flex-col gap-1">
                <span>The robot downloads {step.tag}: {(step.fraction * 100).toFixed(0)} %</span>
                <progress class="progress progress-primary w-full" value={step.fraction} max="1"
                ></progress>
            </div>
        {:else if step.kind === 'restarting'}
            <p>Restarting the robot into the new firmware...</p>
        {:else if step.kind === 'done'}
            {#if step.outcome === 'running'}
                <div class="alert alert-success" role="alert">The robot runs the new firmware.</div>
            {:else}
                <div class="alert alert-error" role="alert">
                    The new firmware did not come up, so the robot rolled back to the previous one.
                </div>
            {/if}
        {:else if step.kind === 'failed'}
            <div class="alert alert-error" role="alert">{step.message}</div>
        {/if}
    </div>
</SettingsCard>
