<script lang="ts">
	import SettingsCard from "$lib/components/SettingsCard.svelte";
	import type { servo } from "$lib/models";
    import MotorOutline from '~icons/mdi/motor-outline';
    import Servo from './servo.svelte';

    let direction = 1
    let angle = 0
    let min_pwm = 1000
    let max_pwm = 2000

    let min_angle = 0
    let max_angle = 0

    let servos:servo[] = [
        {
            channel: 0,
            name: "Front right hip",
            inverted: false,
            angle: angle,
            min_pwm: min_pwm,
            max_pwm: max_pwm,
            min_angle: min_angle,
            max_angle: max_angle,
            center_angle: 0
        }
    ]

</script>

<SettingsCard collapsible={false}>
    <MotorOutline slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    <span slot="title">Servo</span>

    {#each servos as servo}
        <Servo {servo} />
        <div class="divider"></div> 
    {/each}
</SettingsCard>