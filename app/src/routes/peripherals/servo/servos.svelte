<script lang="ts">
	import SettingsCard from "$lib/components/SettingsCard.svelte";
	import type { ServoConfiguration } from "$lib/models";
    import MotorOutline from '~icons/mdi/motor-outline';
    import Servo from './servo.svelte';
	import { api } from "$lib/api";
	import Spinner from "$lib/components/Spinner.svelte";

    let servo_config: ServoConfiguration

    $: updateServoConfig(servo_config)

    const updateServoConfig = async (servo_config: ServoConfiguration) => {
        let result = await api.post('/api/servo/configuration', servo_config)
    }

    const getServoConfig = async () => {
        let result = await api.get<ServoConfiguration>('/api/servo/configuration')
        if (result.isOk()) {
            servo_config = result.inner
            return result.inner
        }
    }
</script>

<SettingsCard collapsible={false}>
    <MotorOutline slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    <span slot="title">Servo</span>

    {#await getServoConfig() }
        <Spinner />
    {:then _}
        <div class="flex flex-col">
            <h2 class="text-lg">General servo configuration</h2>
            <span class="mb-1 flex justify-between items-center">
                Servo Oscillator Frequency <input type="number" bind:value={servo_config.servo_oscillator_frequency} class="input input-bordered input-sm max-w-xs"/>
            </span>
            <span class="flex justify-between items-center mb-1">
                Servo PWM Frequency <input type="number" bind:value={servo_config.servo_pwm_frequency} class="input input-bordered input-sm max-w-xs"/>
            </span>
            <span class="flex justify-between items-center gap-1">
               Is active <input type="checkbox" bind:value={servo_config.is_active} class="toggle"/>
           </span>
        </div>
        <div class="divider"></div> 
        {#each servo_config.servos as servo}
            <Servo {servo} />
            <div class="divider"></div> 
        {/each}
    {/await}
</SettingsCard>