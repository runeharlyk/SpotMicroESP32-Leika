<script lang="ts">
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import type { ServoConfiguration, Servo } from '$lib/models';
	import MotorOutline from '~icons/mdi/motor-outline';
	import ServoController from './servo.svelte';
	import { api } from '$lib/api';
	import Spinner from '$lib/components/Spinner.svelte';
	import { socket } from '$lib/stores';
	import { onMount } from 'svelte';

	let servo_config: ServoConfiguration;
	let servos: Servo[];
	let last_servo_config: ServoConfiguration;

	let isLoading = true;

	$: updateServoConfig(servo_config, servos);

	const updateServoConfig = async (servo_config: ServoConfiguration, servos: Servo[]) => {
		if (!servo_config) return;
		const changes: { [key: string]: any } = {};
		for (const key of Object.keys(servo_config)) {
			if (key == 'servos') {
				for (let i = 0; i < servo_config.servos.length; i++) {
					for (const servo_key of Object.keys(servo_config.servos[i])) {
						if (
							JSON.stringify(servo_config.servos[i][servo_key as keyof Servo]) !==
							JSON.stringify(last_servo_config.servos[i][servo_key as keyof Servo])
						) {
							if (!changes.servos) changes.servos = [];
							if (!changes.servos[i]) changes.servos[i] = {};
							changes.servos[i][servo_key as keyof Servo] =
								servo_config.servos[i][servo_key as keyof Servo];
							changes.servos[i].channel = servo_config.servos[i].channel;
						}
					}
				}
				continue;
			}
			if (
				JSON.stringify(servo_config[key as keyof ServoConfiguration]) !==
				JSON.stringify(last_servo_config[key as keyof ServoConfiguration])
			) {
				changes[key as keyof ServoConfiguration] = servo_config[key as keyof ServoConfiguration];
			}
		}

		if (Object.keys(changes).length > 0) {
			socket.sendEvent('servoConfiguration', changes);
			last_servo_config = structuredClone(servo_config);
		}
	};

    const sweep = (event:any) => {
        let channel = event.detail.channel;
        socket.sendEvent('servoConfiguration', {servos:[{channel, sweep: true}]});
    };

	onMount(() => {
		socket.on('servoConfiguration', (data: ServoConfiguration) => {
			isLoading = false;
			servo_config = data;
			servos = data.servos;
			last_servo_config = structuredClone(data);
		});
	});
</script>

<SettingsCard collapsible={false}>
	<MotorOutline slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
	<span slot="title">Servo</span>

	{#if isLoading}
		<Spinner />
	{:else}
		<div class="flex flex-col">
			<h2 class="text-lg">General servo configuration</h2>
			<span class="mb-1 flex justify-between items-center">
				Servo Oscillator Frequency <input
					type="number"
					bind:value={servo_config.servo_oscillator_frequency}
					class="input input-bordered input-sm max-w-xs"
				/>
			</span>
			<span class="flex justify-between items-center mb-1">
				Servo PWM Frequency <input
					type="number"
					bind:value={servo_config.servo_pwm_frequency}
					class="input input-bordered input-sm max-w-xs"
				/>
			</span>
			<span class="flex justify-between items-center gap-1">
				Is active <input type="checkbox" bind:checked={servo_config.is_active} class="toggle" />
			</span>
		</div>
		<div class="divider"></div>
		{#each servos as servo}
			<ServoController bind:servo on:sweep={sweep} />
			<div class="divider"></div>
		{/each}
	{/if}
</SettingsCard>
