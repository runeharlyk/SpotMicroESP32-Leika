<script lang="ts">
</script>

<div class="w-full h-full absolute top-0 p-4 flex justify-center items-center">
    <div class="bg-gray-800 rounded-md p-4 w-full h-1/2">
        <h1 class="text-lg border-b border-gray-600">Servo calibration</h1>
        <div class="grid grid-cols-2">
            <label for="enable">Enable</label>
            <input bind:value={isEnable} type="checkbox" id="enable" />
        </div>
        <div class="grid grid-cols-2">
            <label for="inverted">Inverted</label>
            <input bind:value={isInverted} type="checkbox" id="Inverted" />
        </div>
        <div class="grid grid-cols-2">
            <label for="pwm">Pwm ({pwm})</label>
            <div class="flex">
                <button class="p-2" on:click={() => pwm--}>-</button>
                <input bind:value={pwm} type="range" id="pwm" min={0} max={700}>
                <button class="p-2" on:click={() => pwm++}>+</button>
            </div>
        </div>
    </div>
</div>
