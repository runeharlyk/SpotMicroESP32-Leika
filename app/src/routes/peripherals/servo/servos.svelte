<script lang="ts">
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import type { ServoConfiguration, Servo } from '$lib/types/models';
	import MotorOutline from '~icons/mdi/motor-outline';
	import ServoController from './servo.svelte';
	import Spinner from '$lib/components/Spinner.svelte';

	import { socket } from '$lib/stores';
	import { onDestroy, onMount } from 'svelte';
	import { throttler as Throttler } from '$lib/utilities';

	let isLoading = false;

    let active = false

    let servoId = 0

    const throttler = new Throttler()

    const sweep = (event:any) => {
        let channel = event.detail.channel;
        socket.sendEvent('servoConfiguration', {servos:[{channel, sweep: true}]});
    };

    const activateServo = (event:any) => {
        socket.sendEvent('servoState', {'active':1});
    };

    const deactivateServo = (event:any) => {
        socket.sendEvent('servoState', {'active':0});
    };

    let pwm = 306;

    const updatePWM = () => {
        throttler.throttle(() => {
            socket.sendEvent('servoPWM', {servo_id:servoId, pwm});
        }, 10)
    }
</script>

<SettingsCard collapsible={false}>
	<MotorOutline slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
	<span slot="title">Servo</span>
    <input type="range" min="200" max="400" bind:value={pwm} on:input={updatePWM} class="w-full h-2 bg-gray-200 rounded-lg appearance-none cursor-pointer dark:bg-gray-700">

	{#if isLoading}
		<Spinner />
	{:else}
		<div class="flex flex-col">
			<h2 class="text-lg">General servo configuration</h2>
            <span class="flex items-center gap-2">
                <label for="servoId">Servo active{servoId}</label>
                <input type="checkbox" class="toggle" bind:checked={active} on:change={active ? deactivateServo : activateServo}>
            </span>
        </div>
	{/if}
</SettingsCard>
