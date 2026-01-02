<script lang="ts">
    import nipplejs from 'nipplejs'
    import { onMount } from 'svelte'
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
    import {
        HumanInputData,
        ModeData,
        ModesEnum,
        WalkGaitData,
        WalkGaits
    } from '$lib/platform_shared/websocket_message'

    let left: nipplejs.JoystickManager
    let right: nipplejs.JoystickManager

    let data: HumanInputData = HumanInputData.create({
        left: { x: 0, y: 0 },
        right: { x: 0, y: 0 },
        height: 0,
        s1: 0,
        speed: 0
    })

    $effect(() => {
        if ($hasGamepad) {
            notifications.success('🎮 Gamepad connected', 3000)
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
        if (b[0]?.justPressed) mode.set(ModeData.create({ mode: ModesEnum.WALK }))
        if (b[1]?.justPressed) mode.set(ModeData.create({ mode: ModesEnum.STAND }))
        if (b[2]?.justPressed) mode.set(ModeData.create({ mode: ModesEnum.REST }))
        if (b[3]?.justPressed) mode.set(ModeData.create({ mode: ModesEnum.DEACTIVATED }))
        if (b[12]?.justPressed)
            input.update(inputData => {
                inputData.height = Math.min(inputData.height + 0.1, 1)
                return inputData
            })
        if (b[13].justPressed)
            input.update(inputData => {
                inputData.height = Math.min(inputData.height - 0.1, 1)
                return inputData
            })
    })

    onMount(() => {
        left = nipplejs.create({
            zone: document.getElementById('left') as HTMLElement,
            color: '#15191e80',
            dynamicPage: true,
            mode: 'static',
            restOpacity: 1
        })

        right = nipplejs.create({
            zone: document.getElementById('right') as HTMLElement,
            color: '#15191e80',
            dynamicPage: true,
            mode: 'static',
            restOpacity: 1
        })

        left.on('move', (_, data) => handleJoyMove('left', data.vector))
        left.on('end', () => handleJoyMove('left', { x: 0, y: 0 }))
        right.on('move', (_, data) => handleJoyMove('right', data.vector))
        right.on('end', () => handleJoyMove('right', { x: 0, y: 0 }))
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
        mode.set(ModeData.create({ mode: modeValue }))
    }

    const changeWalkGait = (walkGaitValue: WalkGaits) => {
        walkGait.set(WalkGaitData.create({ gait: walkGaitValue }))
    }
</script>

<div class="absolute top-0 left-0 w-screen h-screen">
    <div class="absolute top-0 left-0 h-full w-full flex max-[599px]:hidden">
        <div id="left" class="flex w-60 items-center justify-end"></div>
        <div class="flex-1"></div>
        <div id="right" class="flex w-60 items-center"></div>
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
    <div class="absolute bottom-0 z-10 flex items-end pointer-events-none">
        <div
            class="flex items-center flex-col backdrop-blur-sm bg-base-300/60 p-3 pb-2 gap-2 rounded-tr-2xl border-t border-base-content/5 pointer-events-auto"
        >
            <VerticalSlider
                min={0}
                max={1}
                step={0.01}
                oninput={e => handleRange(Number((e.target as HTMLInputElement).value), 'height')}
            />
            <label for="height" class="text-xs font-medium opacity-70">Ht</label>
        </div>
        <div
            class="flex items-end gap-4 backdrop-blur-sm bg-base-300/60 h-min rounded-tr-2xl pl-0 p-3 border-t border-r border-base-content/5 pointer-events-auto"
        >
            <div class="join shadow-lg">
                {#each modes as modeValue (modeValue)}
                    <button
                        class="btn join-item btn-sm transition-all duration-200"
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
                            class="btn join-item btn-xs transition-all duration-200"
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
                            name="s1"
                            min="0"
                            step="0.01"
                            max="1"
                            oninput={e =>
                                handleRange(Number((e.target as HTMLInputElement).value), 's1')}
                            class="range range-xs range-primary"
                        />
                    </div>
                    <div class="flex flex-col gap-1">
                        <label for="speed" class="text-xs font-medium opacity-70">Speed</label>
                        <input
                            type="range"
                            name="speed"
                            min="0"
                            step="0.01"
                            max="1"
                            oninput={e =>
                                handleRange(Number((e.target as HTMLInputElement).value), 'speed')}
                            class="range range-xs range-primary"
                        />
                    </div>
                </div>
            {/if}
        </div>
    </div>
</div>

<svelte:window onkeyup={handleKeyup} onkeydown={handleKeyup} />
