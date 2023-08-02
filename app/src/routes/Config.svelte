<script lang="ts">
    import { Card, CardHeader } from "../components/index";
	import { socket, isConnected } from "../lib/socket";
    import { throttler } from "../lib/throttle";

    let throttle = new throttler();
    let throttle_timing = 25;
    let pwm = 300
    let servo = 0
    
    let min = 0;
    let halfWay = 0;
    let max = 0;
    let overallRange = 0;
        
    $: buffer = (overallRange - 180) / 2
    $: conversionRate = (max - min) / overallRange;
    $: zeroMark = (conversionRate * buffer) + min;
    $: oneEightyMark = (conversionRate * buffer) + halfWay;

    $: pwm, throttle.throttle(() => $isConnected ? $socket.send(JSON.stringify({servo, pwm, action:0})) : console.log("Is not connected yet"), throttle_timing);

</script>

<div class="w-full h-full absolute top-0 p-4 flex justify-center items-center">
    <Card class="w-full h-1/2">
        <CardHeader>Servo calibration</CardHeader>
        <div>Servo</div>
        <input type="number" class="bg-background" bind:value={servo}>
        <div class="flex w-full">
            <label for="pwm">Pwm ({pwm})</label>
            <div class="flex w-full">
                <button class="p-2" on:click={() => pwm--}>-</button>
                <input class="flex-1" bind:value={pwm} type="range" id="pwm" min={0} max={700}>
                <button class="p-2" on:click={() => pwm++}>+</button>
            </div>
        </div>
        <div class="grid grid-cols-2 w-1/2">
            <div>Min pwm</div>
            <input type="number" class="bg-background" bind:value={min}>
            <div>Pwm at Halfway from min</div> <input type="number" class="bg-background" bind:value={halfWay}>
            <div>Max pwm</div> <input type="number" class="bg-background" bind:value={max}>
            <div>Overall angle</div> <input type="number" class="bg-background" bind:value={overallRange}>
            <div>Buffer</div><div>{buffer}</div>
            <div>Conversion rate</div><div>{conversionRate}</div>
            <div>pwm for 0° mark</div><div>{zeroMark}</div>
            <div>pwm for 180° Mark</div><div>{oneEightyMark}</div>
        </div>
    </Card>
</div>
