<script lang="ts">
    import { api } from '$lib/api'
    import Spinner from '$lib/components/Spinner.svelte'
    import { CameraSettings, Request, type Response as ProtoResponse } from '$lib/platform_shared/api'

    let settings = $state<CameraSettings>(CameraSettings.create({}))

    const getCameraSettings = async () => {
        const result = await api.get<ProtoResponse>('/api/camera/settings')
        if (result.isErr()) {
            console.error('An error occurred', result.inner)
            return
        }
        if (result.inner.cameraSettings) {
            settings = result.inner.cameraSettings
        }
    }

    const updateCameraSettings = async () => {
        const request = Request.create({
            cameraSettings: settings
        })
        const result = await api.post_proto<ProtoResponse>('/api/camera/settings', request)
        if (result.isErr()) {
            console.error('An error occurred', result.inner)
            return
        }
        if (result.inner.cameraSettings) {
            settings = result.inner.cameraSettings
        }
    }

    // Helper to convert number (0/1) to boolean for checkbox binding
    const getVflip = () => settings.vflip !== 0
    const setVflip = (value: boolean) => (settings.vflip = value ? 1 : 0)
    const getHmirror = () => settings.hmirror !== 0
    const setHmirror = (value: boolean) => (settings.hmirror = value ? 1 : 0)
</script>

{#await getCameraSettings()}
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
                onchange={(e) => setVflip(e.currentTarget.checked)}
            />
        </label>

        <label class="cursor-pointer flex items-center justify-between">
            Horizontal flip
            <input
                type="checkbox"
                class="toggle"
                checked={getHmirror()}
                onchange={(e) => setHmirror(e.currentTarget.checked)}
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
{/await}
