<script lang="ts">
    import SettingsCard from '$lib/components/SettingsCard.svelte';
    import type { ServoConfiguration, Servo } from '$lib/types/models';
    import ServoController from './servo.svelte';
    import Spinner from '$lib/components/Spinner.svelte';

    import { socket } from '$lib/stores';
    import { onDestroy, onMount } from 'svelte';
    import { throttler as Throttler } from '$lib/utilities';
    import { MotorOutline } from '$lib/components/icons';
    import Selector from '$lib/components/widget/Selector.svelte';

    let isLoading = false;

    let active = false;

    let servoId = 0;

    const throttler = new Throttler();

    const states = ['Deactivated', 'Active', 'Single_PWM', 'All_PWM'] as const;

    enum ServoState {
        Deactivated,
        Active,
        Single_PWM,
        All_PWM
    }

    type States = (typeof ServoState)[number];

    let state: States = 'Deactivated';

    const sweep = (event: any) => {
        let channel = event.detail.channel;
        socket.sendEvent('servoConfiguration', { servos: [{ channel, sweep: true }] });
    };

    let pwm = 306;
    let angle = 90;
    let controlType = 'PWM';

    const updatePosition = () => {
        throttler.throttle(() => {
            socket.sendEvent(`servo${controlType}`, { servo_id: servoId, pwm });
        }, 10);
    };

    const updateState = () => {
        socket.sendEvent('servoState', { data: states.indexOf(state) });
    };
</script>

<SettingsCard collapsible={false}>
    <MotorOutline slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    <span slot="title">Servo</span>

    <div class="flex gap-4 items-center">
        State
        <Selector bind:selectedOption={state} options={states} on:change={updateState} />
    </div>
    {#if state === 'Single_angle' || state === 'Single_PWM'}
        <div>
            Servo number
            <input class="input" type="number" bind:value={servoId} />
        </div>
    {/if}

    <div class="flex items-center">
        <span class="w-32">Control option:</span>
        <Selector bind:selectedOption={controlType} options={['PWM', 'Degree']} />
    </div>
    {#if controlType === 'PWM'}
        <input
            type="range"
            min="100"
            max="800"
            bind:value={pwm}
            on:input={updatePosition}
            class="w-full h-2 bg-gray-200 rounded-lg appearance-none cursor-pointer dark:bg-gray-700"
        />
        <div>{pwm} pwm</div>
    {:else}
        <input
            type="range"
            min="0"
            max="180"
            bind:value={angle}
            on:input={updatePosition}
            class="w-full h-2 bg-gray-200 rounded-lg appearance-none cursor-pointer dark:bg-gray-700"
        />
        <div>{angle} degrees</div>
    {/if}
</SettingsCard>
