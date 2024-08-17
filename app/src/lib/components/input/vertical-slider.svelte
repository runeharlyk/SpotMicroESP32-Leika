<script lang="ts">
    export let min = 0;
    export let max = 100;
    export let step = 1;
    export let value = (max - min) / 2;
</script>
  
<input
    type="range"
    style="writing-mode: vertical-lr; direction: rtl"
    class="cursor-pointer"
    min={min}
    max={max}
    step={step}
    bind:value
    on:input
    on:change
/>

<style>
input[type=range]::-webkit-slider-runnable-track {
  background: oklch(var(--p)/1);
  border-radius: var(--rounded-box, 1rem);
} 
</style>