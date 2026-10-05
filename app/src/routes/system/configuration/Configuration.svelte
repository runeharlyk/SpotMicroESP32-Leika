<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte'
    import { Save, DownloadIcon, UploadIcon } from '$lib/components/icons'
    import { connectionFeatures } from '$lib/stores/featureFlags'
    import { mode } from '$lib/stores/model-store'
    import { ModeData, ModesEnum } from '$lib/platform_shared/message'
    import {
        applyConfig,
        exportConfig,
        parseConfig,
        planImport,
        serializeConfig,
        type ImportPlan,
        type RobotConfig
    } from '$lib/robot-config'
    import { saveFile } from '$lib/utilities/save-file'

    type Step =
        | { kind: 'idle' }
        | { kind: 'busy'; doing: string }
        | { kind: 'chosen'; config: RobotConfig; plan: ImportPlan }
        | { kind: 'done'; message: string }
        | { kind: 'failed'; message: string }

    let step = $state<Step>({ kind: 'idle' })
    let fileInput: HTMLInputElement | undefined = $state()

    const deactivated = $derived($mode.mode === ModesEnum.DEACTIVATED)
    const working = $derived(step.kind === 'busy')

    const fail = (error: unknown) => (step = { kind: 'failed', message: (error as Error).message })

    async function save() {
        step = { kind: 'busy', doing: 'Reading the settings' }
        try {
            const config = await exportConfig()
            const name = (config.robot.name || 'robot').replace(/[^\w-]+/g, '-')
            saveFile(
                serializeConfig(config),
                `${name}-${config.exportedAt.slice(0, 10)}.json`,
                'application/json'
            )
            step = { kind: 'done', message: 'Configuration saved.' }
        } catch (error) {
            fail(error)
        }
    }

    async function pickFile(event: Event) {
        const file = (event.currentTarget as HTMLInputElement).files?.[0]
        fileInput!.value = ''
        if (!file) return
        try {
            const config = parseConfig(await file.text())
            if (!$connectionFeatures) throw new Error('No robot connected')
            step = { kind: 'chosen', config, plan: planImport(config, $connectionFeatures) }
        } catch (error) {
            fail(error)
        }
    }

    async function load(config: RobotConfig, plan: ImportPlan) {
        step = { kind: 'busy', doing: 'Writing the settings' }
        try {
            await applyConfig(config, plan)
            step = { kind: 'done', message: 'Configuration loaded.' }
        } catch (error) {
            fail(error)
        }
    }

    const deactivate = () => mode.set(ModeData.create({ mode: ModesEnum.DEACTIVATED }))
</script>

<SettingsCard collapsible={false}>
    {#snippet icon()}
        <Save class="mr-2 h-6 w-6 shrink-0 self-end" />
    {/snippet}
    {#snippet title()}
        <span>Configuration</span>
    {/snippet}

    <div class="flex flex-col gap-4">
        <p>
            Saves the servo calibration and the peripheral settings (IMU mounting, compass
            calibration, pins, LED strip) to a file, and loads them back, here or on another robot.
            WiFi settings stay on the robot: it never sends its passwords.
        </p>

        <div class="flex flex-wrap gap-2">
            <button class="btn btn-primary" disabled={working} onclick={save}>
                <DownloadIcon class="mr-2 h-5 w-5" />Save to a file
            </button>
            <button class="btn" disabled={working} onclick={() => fileInput!.click()}>
                <UploadIcon class="mr-2 h-5 w-5" />Load a file
            </button>
            <input
                type="file"
                accept=".json"
                class="hidden"
                bind:this={fileInput}
                onchange={pickFile}
            />
        </div>

        {#if step.kind === 'busy'}
            <p>{step.doing}...</p>
        {:else if step.kind === 'chosen'}
            {@const { config, plan } = step}
            <div class="flex flex-col gap-2">
                <p>
                    From <strong>{config.robot.name || 'an unnamed robot'}</strong>
                    ({config.robot.variant || 'no variant'}), saved {config.exportedAt.slice(
                        0,
                        10
                    )}. Loading it writes the servo calibration and the peripheral settings{(
                        plan.variant
                    ) ?
                        `, switches the variant to ${plan.variant}`
                    :   ''}{plan.rename ? ` and restores the name ${plan.rename}` : ''}.
                </p>
                {#each plan.warnings as warning (warning)}
                    <div class="alert alert-warning" role="alert">{warning}</div>
                {/each}
                {#if plan.variant && !deactivated}
                    <div class="alert alert-warning" role="alert">
                        <span>The robot switches its variant only while deactivated.</span>
                        <button class="btn btn-sm" onclick={deactivate}>Deactivate</button>
                    </div>
                {/if}
                <div>
                    <button
                        class="btn btn-primary"
                        disabled={!!plan.variant && !deactivated}
                        onclick={() => load(config, plan)}
                    >
                        Load
                    </button>
                </div>
            </div>
        {:else if step.kind === 'done'}
            <div class="alert alert-success" role="alert">{step.message}</div>
        {:else if step.kind === 'failed'}
            <div class="alert alert-error" role="alert">{step.message}</div>
        {/if}
    </div>
</SettingsCard>
