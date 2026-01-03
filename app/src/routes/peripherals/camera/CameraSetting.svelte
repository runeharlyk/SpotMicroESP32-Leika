<script lang="ts">
    import { api } from '$lib/api'
    import Spinner from '$lib/components/Spinner.svelte'
    import {
        type CameraSettingsData,
        CameraSettingsData as CameraSettingsDataProto
    } from '$lib/platform_shared/message'

    let settings: CameraSettingsData | null = $state(null)

    const getCameraSettings = async () => {
        const result = await api.get('/api/camera/settings', CameraSettingsDataProto)
        if (result.isErr()) {
            console.error('An error occurred', result.inner)
            return
        }
        settings = result.inner
    }

    const updateCameraSettings = async () => {
        if (!settings) return
        const result = await api.post('/api/camera/settings', settings, CameraSettingsDataProto, CameraSettingsDataProto)
        if (result.isErr()) {
            console.error('An error occurred', result.inner)
            return
        }
        settings = result.inner
    }

    function toggleVflip() {
        if (!settings) return
        settings.vflip = settings.vflip ? 0 : 1
    }

    function toggleHmirror() {
        if (!settings) return
        settings.hmirror = settings.hmirror ? 0 : 1
    }
</script>

{#await getCameraSettings()}
    <Spinner />
{:then}
    {#if settings}
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
                <input type="checkbox" class="toggle" checked={settings.vflip === 1} onchange={toggleVflip} />
            </label>

            <label class="cursor-pointer flex items-center justify-between">
                Horizontal flip
                <input type="checkbox" class="toggle" checked={settings.hmirror === 1} onchange={toggleHmirror} />
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
    {/if}
{/await}
