<script lang="ts">
    import { resolve } from '$app/paths'
    import { socket } from '$lib/stores'
    import { builtInClipNames } from '$lib/animation/robot'
    import { AnimationState } from '$lib/platform_shared/message'
    import { animationStatus as robotStatus } from '$lib/robot/state'
    import { drivesSimulation, playDrivenClip, simulation, stopDrivenClip } from '$lib/stores/drive'
    import { notifications } from '$lib/components/toasts/notifications'

    // A built-in clip at its default values, played on the robot the controls drive; the animations page has the rest.
    const simulationPlayer = simulation.player
    const simulationStatus = simulation.animationStatus
    let name = $state(builtInClipNames[0] ?? '')

    const usable = $derived($drivesSimulation ? $simulationPlayer !== null : $socket)
    const status = $derived($drivesSimulation ? $simulationStatus : $robotStatus)
    const playing = $derived(
        status && status.state !== AnimationState.ANIM_IDLE ? status.name : null
    )

    const attempt = (action: () => Promise<void>) =>
        action().catch((error: Error) => notifications.error(error.message, 5000))
</script>

<div class="flex flex-wrap items-center gap-2">
    <select
        class="select select-sm pointer-coarse:select-md w-auto min-w-0"
        aria-label="Clip"
        bind:value={name}
    >
        {#each builtInClipNames as clip (clip)}
            <option value={clip}>{clip}</option>
        {/each}
    </select>
    <button
        class="btn btn-primary btn-sm pointer-coarse:btn-md"
        disabled={!usable}
        onclick={() => attempt(() => playDrivenClip(name, []))}
    >
        Play
    </button>
    <button
        class="btn btn-sm pointer-coarse:btn-md"
        disabled={!usable}
        onclick={() => attempt(stopDrivenClip)}
    >
        Stop
    </button>
    {#if $drivesSimulation && !usable}
        <span class="text-xs opacity-70">The simulation's firmware controller plays clips</span>
    {/if}
    {#if playing}
        <span class="badge badge-info badge-sm" aria-live="polite">Playing {playing}</span>
    {/if}
    <a class="link text-xs" href={resolve('/animations')}>All clips</a>
</div>
