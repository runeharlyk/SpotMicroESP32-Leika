<script lang="ts">
    import nipplejs from 'nipplejs'
    import { haptics } from '$lib/utilities'
    import { onDestroy, onMount } from 'svelte'
    import {
        input,
        mode,
        walkGait,
        modes,
        modeLabels,
        walkGaits,
        walkGaitLabels
    } from '$lib/stores'
    import type { vector } from '$lib/types/models'
    import { VerticalSlider } from '$lib/components/input'
    import { gamepadAxes, gamepadButtonsEdges, hasGamepad } from '$lib/stores/gamepad'
    import { notifications } from '$lib/components/toasts/notifications'
    import { ModeData, ModesEnum, WalkGaitData, WalkGaits } from '$lib/platform_shared/message'
    import { gamepadCommand, stepHeight } from '$lib/utilities/gamepad-mapping'

    let left: nipplejs.JoystickManager
    let right: nipplejs.JoystickManager

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
        if (command.mode !== undefined) mode.set(ModeData.create({ mode: command.mode }))
        if (command.heightStep)
            input.update(inputData => {
                inputData.height = stepHeight(inputData.height, command.heightStep)
                return inputData
            })
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

        left.on('move', (_, data) => handleJoyMove('left', data.vector))
        left.on('end', () => handleJoyMove('left', { x: 0, y: 0 }))
        right.on('move', (_, data) => handleJoyMove('right', data.vector))
        right.on('end', () => handleJoyMove('right', { x: 0, y: 0 }))
    })

    onDestroy(() => {
        left?.destroy()
        right?.destroy()
    })

    const handleJoyMove = (key: 'left' | 'right', data: vector) => {
        input.update(inputData => {
            inputData[key] = data
            return inputData
        })
    }

    const handleKeyup = (event: KeyboardEvent) => {
        const down = event.type === 'keydown'
        input.update(data => {
            if (event.key === 'w') data.left!.y = down ? 1 : 0
            if (event.key === 'a') data.left!.x = down ? -1 : 0
            if (event.key === 's') data.left!.y = down ? -1 : 0
            if (event.key === 'd') data.left!.x = down ? 1 : 0
            if (event.key === 'ArrowLeft') data.right!.x = down ? 1 : 0
            if (event.key === 'ArrowRight') data.right!.x = down ? -1 : 0
            return data
        })
    }

    const handleRange = (value: number, key: 'speed' | 'height' | 's1') => {
        input.update(inputData => {
            inputData[key] = value
            return inputData
        })
    }

    const changeMode = (modeValue: ModesEnum) => {
        if (modeValue === ModesEnum.DEACTIVATED) haptics.stop()
        else haptics.modeChange()
        mode.set(ModeData.create({ mode: modeValue }))
    }

    const changeWalkGait = (walkGaitValue: WalkGaits) => {
        walkGait.set(WalkGaitData.create({ gait: walkGaitValue }))
    }
</script>

<div class="absolute top-0 left-0 h-dvh w-screen">
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
        class="absolute bottom-0 z-10 flex max-w-full items-end pointer-events-none pl-[env(safe-area-inset-left)]"
    >
        <div
            class="flex items-center flex-col backdrop-blur-sm bg-base-300/60 p-3 pb-[max(0.5rem,env(safe-area-inset-bottom))] gap-2 rounded-tr-2xl border-t border-base-content/5 pointer-events-auto"
        >
            <VerticalSlider
                min={0}
                max={1}
                step={0.01}
                aria-label="Body height"
                oninput={e => handleRange(Number((e.target as HTMLInputElement).value), 'height')}
            />
            <span class="text-xs font-medium opacity-70" aria-hidden="true">Ht</span>
        </div>
        <div
            class="flex min-w-0 flex-wrap items-end gap-x-4 gap-y-2 backdrop-blur-sm bg-base-300/60 h-min rounded-tr-2xl pl-0 p-3 pb-[max(0.75rem,env(safe-area-inset-bottom))] border-t border-r border-base-content/5 pointer-events-auto"
        >
            <div class="join max-w-full shadow-lg max-sm:grid max-sm:grid-cols-3">
                {#each modes as modeValue (modeValue)}
                    <button
                        class="btn join-item btn-sm pointer-coarse:btn-md transition-all duration-200"
                        class:btn-primary={$mode.mode === modeValue}
                        onclick={() => changeMode(modeValue)}
                    >
                        {modeLabels[modeValue]}
                    </button>
                {/each}
            </div>

            {#if $mode.mode === ModesEnum.WALK}
                <div class="join shadow-md">
                    {#each walkGaits as gaitValue (gaitValue)}
                        <button
                            class="btn join-item btn-xs pointer-coarse:btn-sm transition-all duration-200"
                            class:btn-secondary={$walkGait.gait === gaitValue}
                            onclick={() => changeWalkGait(gaitValue)}
                        >
                            {walkGaitLabels[gaitValue]}
                        </button>
                    {/each}
                </div>

                <div class="flex gap-4">
                    <div class="flex flex-col gap-1">
                        <label for="s1" class="text-xs font-medium opacity-70">Step height</label>
                        <input
                            type="range"
                            id="s1"
                            min="0"
                            step="0.01"
                            max="1"
                            oninput={e =>
                                handleRange(Number((e.target as HTMLInputElement).value), 's1')}
                            class="range range-xs pointer-coarse:range-sm range-primary"
                        />
                    </div>
                    <div class="flex flex-col gap-1">
                        <label for="speed" class="text-xs font-medium opacity-70">Speed</label>
                        <input
                            type="range"
                            id="speed"
                            min="0"
                            step="0.01"
                            max="1"
                            oninput={e =>
                                handleRange(Number((e.target as HTMLInputElement).value), 'speed')}
                            class="range range-xs pointer-coarse:range-sm range-primary"
                        />
                    </div>
                </div>
            {/if}
        </div>
    </div>
</div>

<svelte:window onkeyup={handleKeyup} onkeydown={handleKeyup} />
