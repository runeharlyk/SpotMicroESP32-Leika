<script lang="ts">
    import { robotRequest } from '$lib/robot-request'
    import Spinner from '$lib/components/Spinner.svelte'
    import LoadError from '$lib/components/LoadError.svelte'
    import { notifications } from '$lib/components/toasts/notifications'
    import { CameraSettings } from '$lib/platform_shared/api'

    let settings = $state<CameraSettings>(CameraSettings.create({}))

    const getCameraSettings = async () => {
        const reply = await robotRequest({ cameraSettingsRequest: {} })
        if (!reply.cameraSettings) throw new Error('The robot sent no camera settings')
        settings = reply.cameraSettings
    }

    let loading = $state(getCameraSettings())

    const updateCameraSettings = async () => {
        try {
            const reply = await robotRequest({ cameraSettings: settings })
            if (reply.cameraSettings) settings = reply.cameraSettings
        } catch (error) {
            notifications.error(`Saving camera settings failed: ${(error as Error).message}`, 5000)
        }
    }

    // Helper to convert number (0/1) to boolean for checkbox binding
    const getVflip = () => settings.vflip !== 0
    const setVflip = (value: boolean) => (settings.vflip = value ? 1 : 0)
    const getHmirror = () => settings.hmirror !== 0
    const setHmirror = (value: boolean) => (settings.hmirror = value ? 1 : 0)
</script>

{#await loading}
    <Spinner />
{:then}
    <div class="flex flex-col gap-1">
        <button class="btn btn-primary" type="button" onclick={updateCameraSettings}
            >Update camera settings</button
        >

        <label for="brightness">
            Brightness {settings.brightness}
            <input
                type="range"
                min="-2"
                max="2"
                class="range range-xs"
                bind:value={settings.brightness}
            />
        </label>

        <label for="contrast">
            Contrast {settings.contrast}
            <input
                type="range"
                min="-2"
                max="2"
                class="range range-xs"
                bind:value={settings.contrast}
            />
        </label>

        <label for="framesize">
            FrameSize {settings.framesize}
            <input
                type="range"
                min="0"
                max="10"
                class="range range-xs"
                bind:value={settings.framesize}
            />
        </label>

        <label class="cursor-pointer flex items-center justify-between">
            Vertical flip
            <input
                type="checkbox"
                class="toggle"
                checked={getVflip()}
                onchange={e => setVflip(e.currentTarget.checked)}
            />
        </label>

        <label class="cursor-pointer flex items-center justify-between">
            Horizontal flip
            <input
                type="checkbox"
                class="toggle"
                checked={getHmirror()}
                onchange={e => setHmirror(e.currentTarget.checked)}
            />
        </label>

        <label for="special_effect" class="flex items-center">
            <span class="basis-1/2">Special Effect</span>
            <select
                class="select select-bordered select-sm w-full max-w-xs"
                bind:value={settings.specialEffect}
            >
                <option value={0}>No effect</option>
                <option value={1}>Negative</option>
                <option value={2}>Grayscale</option>
                <option value={3}>Red tint</option>
                <option value={4}>Green tint</option>
                <option value={5}>Blue tint</option>
                <option value={6}>Sepia</option>
            </select>
        </label>
    </div>
{:catch error}
    <LoadError {error} retry={() => (loading = getCameraSettings())} />
{/await}
