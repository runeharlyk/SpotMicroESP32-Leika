<script lang="ts">
    import nipplejs from 'nipplejs'
    import { haptics } from '$lib/utilities'
    import { onDestroy, onMount } from 'svelte'
    import { get } from 'svelte/store'
    import {
        input,
        socket,
        selectableModes,
        modeLabels,
        walkGaits,
        walkGaitLabels,
        variantChoiceNeeded,
        connectionFeatures
    } from '$lib/stores'
    import {
        drive,
        driveGait,
        driveMode,
        drivenGait,
        drivenMode,
        drivesSimulation
    } from '$lib/stores/drive'
    import type { vector } from '$lib/types/models'
    import { gamepadAxes, gamepadButtonsEdges, hasGamepad } from '$lib/stores/gamepad'
    import { shapeStick } from '$lib/utilities/stick'
    import { notifications } from '$lib/components/toasts/notifications'
    import { ModesEnum, WalkGaits, type ControllerData } from '$lib/platform_shared/message'
    import { gamepadCommand, stepHeight } from '$lib/utilities/gamepad-mapping'

    type SliderKey = 'speed' | 'height' | 's1'

    let left: nipplejs.JoystickManager
    let right: nipplejs.JoystickManager
    let adjustmentsOpen = $state(false)

    // While the layout shows the simulation, the controls drive its robot and never the real one (stores/drive.ts).
    const steer = (change: (data: ControllerData) => void) => {
        input.update(data => {
            change(data)
            return data
        })
        drive(get(input))
    }

    $effect(() => {
        if ($hasGamepad) {
            notifications.success('Gamepad connected', 3000)
        }
    })

    $effect(() => {
        handleJoyMove('left', { x: $gamepadAxes[0], y: -$gamepadAxes[1] })
        handleJoyMove('right', { x: $gamepadAxes[2], y: $gamepadAxes[3] })
    })

    $effect(() => {
        if (!$hasGamepad) return
        const b = $gamepadButtonsEdges
        if (!b.length) return
        const command = gamepadCommand(b)
        if (command.mode !== undefined && movable(command.mode)) driveMode(command.mode)
        if (command.heightStep)
            steer(data => (data.height = stepHeight(data.height, command.heightStep)))
    })

    onMount(() => {
        left = nipplejs.create({
            zone: document.getElementById('left') as HTMLElement,
            color: '#15191e80',
            dynamicPage: true,
            mode: 'static',
            position: { left: '50%', top: '50%' },
            restOpacity: 1
        })

        right = nipplejs.create({
            zone: document.getElementById('right') as HTMLElement,
            color: '#15191e80',
            dynamicPage: true,
            mode: 'static',
            position: { left: '50%', top: '50%' },
            restOpacity: 1
        })

        left.on('move', (_, data) => handleJoyMove('left', shapeStick(data.vector)))
        left.on('end', () => handleJoyMove('left', { x: 0, y: 0 }))
        right.on('move', (_, data) => handleJoyMove('right', shapeStick(data.vector)))
        right.on('end', () => handleJoyMove('right', { x: 0, y: 0 }))
    })

    onDestroy(() => {
        left?.destroy()
        right?.destroy()
    })

    const handleJoyMove = (key: 'left' | 'right', stick: vector) =>
        steer(data => (data[key] = stick))

    const handleKeyup = (event: KeyboardEvent) => {
        const down = event.type === 'keydown'
        steer(data => {
            if (event.key === 'w') data.left!.y = down ? 1 : 0
            if (event.key === 'a') data.left!.x = down ? -1 : 0
            if (event.key === 's') data.left!.y = down ? -1 : 0
            if (event.key === 'd') data.left!.x = down ? 1 : 0
            if (event.key === 'ArrowLeft') data.right!.x = down ? 1 : 0
            if (event.key === 'ArrowRight') data.right!.x = down ? -1 : 0
        })
    }

    const handleRange = (value: number, key: SliderKey) => steer(data => (data[key] = value))

    // The robot refuses every other mode until it knows which variant it is; the simulation knows its own.
    const movable = (modeValue: ModesEnum) =>
        $drivesSimulation || !$variantChoiceNeeded || modeValue === ModesEnum.DEACTIVATED

    const changeMode = (modeValue: ModesEnum) => {
        if (modeValue === ModesEnum.DEACTIVATED) haptics.stop()
        else haptics.modeChange()
        driveMode(modeValue)
    }

    // The simulation runs no learned walk.
    const offered: ModesEnum[] = $derived(
        selectableModes(!$drivesSimulation && ($connectionFeatures?.walkNn ?? false))
    )
    const walking = $derived($drivenMode === ModesEnum.WALK)
</script>

<div class="absolute top-0 left-0 h-dvh w-full">
    <!-- Portrait phones put both sticks side by side above the control panel. -->
    <div class="absolute inset-0 flex px-[env(safe-area-inset-left)] max-sm:items-end max-sm:pb-40">
        <div id="left" class="relative h-full w-1/2 max-sm:h-44 sm:w-60"></div>
        <div class="flex-1 max-sm:hidden"></div>
        <div id="right" class="relative h-full w-1/2 max-sm:h-44 sm:w-60"></div>
    </div>
    <div
        class="absolute bottom-0 right-0 p-4 z-10 gap-1.5 flex-col hidden lg:flex opacity-40 hover:opacity-80 transition-opacity duration-300"
    >
        <div class="flex justify-center w-full">
            <kbd class="kbd kbd-sm bg-base-100/80 border-base-content/20 shadow-md">W</kbd>
        </div>
        <div class="flex justify-center gap-1.5 w-full">
            <kbd class="kbd kbd-sm bg-base-100/80 border-base-content/20 shadow-md">A</kbd>
            <kbd class="kbd kbd-sm bg-base-100/80 border-base-content/20 shadow-md">S</kbd>
            <kbd class="kbd kbd-sm bg-base-100/80 border-base-content/20 shadow-md">D</kbd>
        </div>
    </div>
    <div
        class="bg-base-300/60 border-base-content/5 absolute bottom-0 left-0 z-10 flex max-h-[calc(100%-4rem)] w-full flex-col gap-2 overflow-y-auto border-t p-3 pl-[max(0.75rem,env(safe-area-inset-left))] pb-[max(0.75rem,env(safe-area-inset-bottom))] backdrop-blur-sm sm:w-auto sm:max-w-xl sm:rounded-tr-2xl sm:border-r"
    >
        {#if $variantChoiceNeeded && !$drivesSimulation}
            <p class="text-xs opacity-70">Tell the robot which variant it is before it can move.</p>
        {/if}
        <div class="flex items-center gap-2">
            <select
                class="select select-primary select-sm pointer-coarse:select-md w-auto min-w-0"
                aria-label="Mode"
                value={$drivenMode}
                onchange={event => changeMode(Number(event.currentTarget.value) as ModesEnum)}
            >
                {#if !offered.includes($drivenMode)}
                    <option value={$drivenMode} disabled>{modeLabels[$drivenMode]}</option>
                {/if}
                {#each offered as modeValue (modeValue)}
                    <option value={modeValue} disabled={!movable(modeValue)}>
                        {modeLabels[modeValue]}
                    </option>
                {/each}
            </select>
            {#if $socket && !$drivesSimulation}
                <button
                    class="btn btn-sm pointer-coarse:btn-md shrink-0"
                    class:btn-error={$drivenMode === ModesEnum.DEACTIVATED}
                    onclick={() => changeMode(ModesEnum.DEACTIVATED)}
                >
                    Deactivate
                </button>
            {/if}
            <button
                class="btn btn-sm pointer-coarse:btn-md shrink-0"
                aria-expanded={adjustmentsOpen}
                aria-controls="controller-adjustments"
                onclick={() => (adjustmentsOpen = !adjustmentsOpen)}
            >
                Adjustments
            </button>
            {#if $drivesSimulation}
                <span class="badge badge-info badge-sm shrink-0">Simulation only</span>
            {:else if !$socket}
                <span class="badge badge-warning badge-sm shrink-0">Preview</span>
            {/if}
        </div>

        {#if walking}
            <div class="flex items-end gap-3">
                <select
                    class="select select-secondary select-sm pointer-coarse:select-md w-auto min-w-0"
                    aria-label="Gait"
                    value={$drivenGait}
                    onchange={event => driveGait(Number(event.currentTarget.value) as WalkGaits)}
                >
                    {#each walkGaits as gaitValue (gaitValue)}
                        <option value={gaitValue}>{walkGaitLabels[gaitValue]}</option>
                    {/each}
                </select>
                <div class="min-w-0 flex-1">{@render slider('speed', 'Speed')}</div>
            </div>
        {/if}

        <div id="controller-adjustments" hidden={!adjustmentsOpen}>
            <div class="grid grid-cols-2 gap-3">
                <div class="min-w-0">{@render slider('height', 'Body height')}</div>
                {#if walking}
                    <div class="min-w-0">{@render slider('s1', 'Step height')}</div>
                {/if}
            </div>
        </div>
    </div>
</div>

{#snippet slider(key: SliderKey, label: string)}
    <div class="flex items-baseline justify-between gap-2">
        <label for={key} class="text-xs font-medium opacity-70">{label}</label>
        <span class="font-mono text-xs tabular-nums opacity-70">{$input[key].toFixed(2)}</span>
    </div>
    <input
        type="range"
        id={key}
        min="0"
        step="0.01"
        max="1"
        value={$input[key]}
        oninput={e => handleRange(Number((e.target as HTMLInputElement).value), key)}
        class="range range-xs pointer-coarse:range-sm range-primary w-full"
    />
{/snippet}

<svelte:window onkeyup={handleKeyup} onkeydown={handleKeyup} />
