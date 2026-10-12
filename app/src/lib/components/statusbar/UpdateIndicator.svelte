<script lang="ts">
    import { resolve } from '$app/paths'
    import { connectionFeatures } from '$lib/stores/featureFlags'
    import { newerRelease } from '$lib/firmware/release'
    import { Chip } from '../icons'

    const CHECK_INTERVAL_MS = 60 * 60 * 1000

    let available: string | undefined = $state()

    // Checked when a robot connects and hourly after; an unreachable release site just shows nothing.
    $effect(() => {
        const env = $connectionFeatures?.firmwareBuiltTarget
        const running = $connectionFeatures?.firmwareVersion
        available = undefined
        if (!env || !running) return
        let current = true
        const check = () =>
            newerRelease(env, running).then(
                release => current && (available = release?.tag),
                () => {}
            )
        void check()
        const timer = setInterval(check, CHECK_INTERVAL_MS)
        return () => {
            current = false
            clearInterval(timer)
        }
    })
</script>

{#if available}
    <a
        class="btn btn-ghost btn-sm indicator flex-none"
        href={resolve('/system/firmware')}
        title={`Firmware ${available} is available`}
        aria-label={`Firmware ${available} is available`}
    >
        <span class="indicator-item badge badge-info badge-xs">{available}</span>
        <Chip class="h-6 w-6" />
    </a>
{/if}
