<script lang="ts">
    import { skill } from '$lib/stores'
    import { onMount, onDestroy } from 'svelte'

    let targetX = $state(0.5)
    let targetZ = $state(0)
    let targetYaw = $state(0)
    let speed = $state(0.5)

    const status = skill.status
    const isActive = skill.isActive
    const progress = skill.progress

    const presets = [
        { name: 'Forward 0.5m', x: 0.5, z: 0, yaw: 0 },
        { name: 'Forward 1m', x: 1, z: 0, yaw: 0 },
        { name: 'Back 0.5m', x: -0.5, z: 0, yaw: 0 },
        { name: 'Left 0.5m', x: 0, z: 0.5, yaw: 0 },
        { name: 'Right 0.5m', x: 0, z: -0.5, yaw: 0 },
        { name: 'Turn Left 90°', x: 0, z: 0, yaw: 1.57 },
        { name: 'Turn Right 90°', x: 0, z: 0, yaw: -1.57 },
        { name: 'Turn 180°', x: 0, z: 0, yaw: 3.14 }
    ]

    onMount(() => skill.init())
    onDestroy(() => skill.destroy())

    function executeSkill() {
        skill.walk(targetX, targetZ, targetYaw, speed)
    }

    function runPreset(preset: (typeof presets)[0]) {
        skill.walk(preset.x, preset.z, preset.yaw, speed)
    }

    function formatMeters(val: number): string {
        return val.toFixed(3) + 'm'
    }

    function formatDegrees(rad: number): string {
        return ((rad * 180) / Math.PI).toFixed(1) + '°'
    }
</script>

<div class="card bg-base-200 shadow-xl">
    <div class="card-body p-4">
        <h2 class="card-title text-sm flex justify-between">
            Skill Control
            <span class="badge" class:badge-success={$isActive} class:badge-ghost={!$isActive}>
                {$isActive ? 'Active' : 'Idle'}
            </span>
        </h2>

        <div class="grid grid-cols-2 gap-2 text-xs mb-2">
            <div class="stat bg-base-300 rounded-lg p-2">
                <div class="stat-title text-xs">Position</div>
                <div class="stat-value text-sm">
                    {formatMeters($status.x)}, {formatMeters($status.z)}
                </div>
                <div class="stat-desc">Yaw: {formatDegrees($status.yaw)}</div>
            </div>
            <div class="stat bg-base-300 rounded-lg p-2">
                <div class="stat-title text-xs">Distance</div>
                <div class="stat-value text-sm">{formatMeters($status.distance)}</div>
                <div class="stat-desc">Total traveled</div>
            </div>
        </div>

        {#if $isActive}
            <div class="mb-2">
                <div class="flex justify-between text-xs mb-1">
                    <span>Progress</span>
                    <span>{($progress * 100).toFixed(0)}%</span>
                </div>
                <progress class="progress progress-primary w-full" value={$progress} max="1"></progress>
                <div class="text-xs text-base-content/60 mt-1">
                    Target: ({$status.skill_target_x.toFixed(2)}, {$status.skill_target_z.toFixed(2)}, {formatDegrees(
                        $status.skill_target_yaw
                    )})
                </div>
            </div>
        {/if}

        <div class="divider my-1 text-xs">Presets</div>

        <div class="grid grid-cols-4 gap-1">
            {#each presets as preset}
                <button class="btn btn-xs btn-outline" onclick={() => runPreset(preset)}>
                    {preset.name}
                </button>
            {/each}
        </div>

        <div class="divider my-1 text-xs">Custom</div>

        <div class="grid grid-cols-3 gap-2">
            <div class="form-control">
                <label class="label py-0" for="skill-x">
                    <span class="label-text text-xs">X (m)</span>
                </label>
                <input
                    id="skill-x"
                    type="number"
                    step="0.1"
                    bind:value={targetX}
                    class="input input-bordered input-xs w-full"
                />
            </div>
            <div class="form-control">
                <label class="label py-0" for="skill-z">
                    <span class="label-text text-xs">Z (m)</span>
                </label>
                <input
                    id="skill-z"
                    type="number"
                    step="0.1"
                    bind:value={targetZ}
                    class="input input-bordered input-xs w-full"
                />
            </div>
            <div class="form-control">
                <label class="label py-0" for="skill-yaw">
                    <span class="label-text text-xs">Yaw (rad)</span>
                </label>
                <input
                    id="skill-yaw"
                    type="number"
                    step="0.1"
                    bind:value={targetYaw}
                    class="input input-bordered input-xs w-full"
                />
            </div>
        </div>

        <div class="form-control mt-2">
            <label class="label py-0" for="skill-speed">
                <span class="label-text text-xs">Speed: {speed.toFixed(2)}</span>
            </label>
            <input id="skill-speed" type="range" min="0.1" max="1" step="0.05" bind:value={speed} class="range range-xs range-primary" />
        </div>

        <div class="card-actions justify-between mt-2">
            <div class="flex gap-1">
                <button class="btn btn-xs btn-ghost" onclick={() => skill.resetPosition()}>Reset Pos</button>
            </div>
            <div class="flex gap-1">
                <button class="btn btn-xs btn-error" onclick={() => skill.stop()} disabled={!$isActive}>
                    Stop
                </button>
                <button class="btn btn-xs btn-primary" onclick={executeSkill} disabled={$isActive}>
                    Execute
                </button>
            </div>
        </div>
    </div>
</div>
