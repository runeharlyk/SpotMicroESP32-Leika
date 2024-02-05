<script lang="ts">
	import { onMount } from 'svelte';
	import { jointNames } from '../../lib/store';

    type Servo = {
        id: number;
        name: string;
        minPWM: number;
        maxPWM: number;
        pwmFor180: number;
    };

	let servos: any[] = [];

    onMount(() => {
        jointNames.subscribe(data => {
            servos = data.map((name:string, i:number) => {
                return {
                    id: i,
                    name,
                    minPWM: 0,
                    maxPWM: 0,
                    pwmFor180: 0
                };
            });
        })
    })


	let selectedServo: number | null = null;

    function updateServoValue(index: number, field: keyof Servo, value: number): void {
        servos[index] = { ...servos[index], [field]: value };
    }

    const formatServo = (servo:Servo) => {
        const string = servo.name
        const name = string.charAt(0).toUpperCase() + string.split('_').join(' ').slice(1);
        return `${servo.id} ${name}`
    }
</script>
<div>
	<div class="servo-selector">
		<label for="servo-select">Select Servo:</label>
		<select id="servo-select" class="bg-zinc-800" bind:value={selectedServo}>
			{#each servos as servo}
				<option value={servo.id}>{formatServo(servo)}</option>
			{/each}
		</select>
	</div>

{#if selectedServo !== null}
  <div class="mt-5">
    <h2>Servo {formatServo(servos[selectedServo])} Calibration</h2>
    <label for="minPWM">Min PWM:</label>
    <input type="number" id="minPWM" class="bg-zinc-800"
           value={servos[selectedServo].minPWM}
           on:blur={(event) => updateServoValue(selectedServo, 'minPWM', Number(event.target.value))} />

    <label for="maxPWM">Max PWM:</label>
    <input type="number" id="maxPWM" class="bg-zinc-800"
           value={servos[selectedServo].maxPWM}
           on:blur={(event) => updateServoValue(selectedServo, 'maxPWM', Number(event.target.value))} />

    <label for="pwmFor180">PWM for 180°:</label>
    <input type="number" id="pwmFor180" class="bg-zinc-800"
           value={servos[selectedServo].pwmFor180}
           on:blur={(event) => updateServoValue(selectedServo, 'pwmFor180', Number(event.target.value))} />
  </div>
{/if}
</div>
