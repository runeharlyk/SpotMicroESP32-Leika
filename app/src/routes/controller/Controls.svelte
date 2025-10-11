<script lang="ts">
    import nipplejs from 'nipplejs'
    import { onMount } from 'svelte'
    import { capitalize, throttler } from '$lib/utilities'
    import {
        input,
        outControllerData,
        mode,
        modes,
        type Modes,
        ModesEnum,
        WalkGaits,
        walkGait,
        walkGaitLabels
    } from '$lib/stores'
    import type { vector } from '$lib/types/models'
    import { VerticalSlider } from '$lib/components/input'
    import { gamepadAxes, hasGamepad } from '$lib/stores/gamepad'
    import { notifications } from '$lib/components/toasts/notifications'

    let throttle = new throttler()
    let left: nipplejs.JoystickManager
    let right: nipplejs.JoystickManager

    let throttle_timing = 40
    let data = new Array(7)

    $effect(() => {
        if ($hasGamepad) {
            notifications.success('🎮 Gamepad connected', 3000)
        }
    })

    $effect(() => {
        handleJoyMove('left', { x: $gamepadAxes[0], y: -$gamepadAxes[1] })
        handleJoyMove('right', { x: $gamepadAxes[2], y: $gamepadAxes[3] })
    })

    // TODO React to button press
    //   $effect(() => {
    //     if ($gamepadButtons.length === 0) return
    //
    //   })

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
        throttle.throttle(updateData, throttle_timing)
    }

    const updateData = () => {
        data[0] = $input.left.x
        data[1] = $input.left.y
        data[2] = $input.right.x
        data[3] = $input.right.y
        data[4] = $input.height
        data[5] = $input.speed
        data[6] = $input.s1

        outControllerData.set(data)
    }

    const handleKeyup = (event: KeyboardEvent) => {
        const down = event.type === 'keydown'
        input.update(data => {
            if (event.key === 'w') data.left.y = down ? 1 : 0
            if (event.key === 'a') data.left.x = down ? 1 : 0
            if (event.key === 's') data.left.y = down ? -1 : 0
            if (event.key === 'd') data.left.x = down ? -1 : 0
            return data
        })
        throttle.throttle(updateData, throttle_timing)
    }

    const handleRange = (event: Event, key: 'speed' | 'height' | 's1') => {
        const value: number = Number((event.target as HTMLInputElement).value)

        input.update(inputData => {
            inputData[key] = value
            return inputData
        })
        throttle.throttle(updateData, throttle_timing)
    }

    const changeMode = (modeValue: Modes) => {
        mode.set(modes.indexOf(modeValue))
    }

    const changeWalkGait = (walkGaitValue: WalkGaits) => {
        walkGait.set(walkGaitValue)
    }
</script>

<div class="absolute top-0 left-0 w-screen h-screen">
    <div class="absolute top-0 left-0 h-full w-full flex portrait:hidden">
        <div id="left" class="flex w-60 items-center justify-end"></div>
        <div class="flex-1"></div>
        <div id="right" class="flex w-60 items-center"></div>
    </div>
    <div class="absolute bottom-0 right-0 p-4 z-10 gap-2 flex-col hidden lg:flex">
        <div class="flex justify-center w-full">
            <kbd class="kbd">W</kbd>
        </div>
        <div class="flex justify-center gap-2 w-full">
            <kbd class="kbd">A</kbd>
            <kbd class="kbd">S</kbd>
            <kbd class="kbd">D</kbd>
        </div>
        <div class="flex justify-center w-full"></div>
    </div>
    <div class="absolute bottom-0 z-10 flex items-end">
        <div
            class="flex items-center flex-col bg-base-300 bg-opacity-50 p-3 pb-2 gap-2 rounded-tr-xl"
        >
            <VerticalSlider
                min={0}
                max={1}
                step={0.01}
                oninput={(e: Event) => handleRange(e, 'height')}
            />
            <label for="height">Ht</label>
        </div>
        <div
            class="flex items-end gap-4 bg-base-300 bg-opacity-50 h-min rounded-tr-xl pl-0 p-3 portrait:hidden"
        >
            <div class="join">
                {#each modes as modeValue}
                    <button
                        class="btn join-item"
                        class:btn-primary={$mode === modes.indexOf(modeValue)}
                        onclick={() => changeMode(modeValue)}
                    >
                        {capitalize(modeValue)}
                    </button>
                {/each}
            </div>

            {#if $mode === ModesEnum.Walk}
                <div class="join">
                    {#each Object.values(WalkGaits) as gaitValue}
                        {#if typeof gaitValue === 'number'}
                            <button
                                class="btn join-item btn-sm"
                                class:btn-secondary={$walkGait === gaitValue}
                                onclick={() => changeWalkGait(gaitValue)}
                            >
                                {walkGaitLabels[gaitValue]}
                            </button>
                        {/if}
                    {/each}
                </div>

                <div class="flex gap-4">
                    <div>
                        <label for="s1">S1</label>
                        <input
                            type="range"
                            name="s1"
                            min="0"
                            step="0.01"
                            max="1"
                            oninput={e => handleRange(e, 's1')}
                            class="range range-sm range-primary"
                        />
                    </div>
                    <div>
                        <label for="speed">Speed</label>
                        <input
                            type="range"
                            name="speed"
                            min="0"
                            step="0.01"
                            max="1"
                            oninput={e => handleRange(e, 'speed')}
                            class="range range-sm range-primary"
                        />
                    </div>
                </div>
            {/if}
        </div>
    </div>
</div>

<svelte:window onkeyup={handleKeyup} onkeydown={handleKeyup} />
