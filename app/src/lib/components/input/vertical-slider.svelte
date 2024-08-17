<script lang="ts">
    export let min = 0;
    export let max = 100;
    export let step = 1;
    export let value = (max - min) / 2;
</script>
  
<input
    type="range"
    orient="vertical"
    min={min}
    max={max}
    step={step}
    bind:value
    on:input
    on:change
/>

<style>
input[type=range][orient=vertical] {
  -webkit-appearance: slider-vertical;
  width: 1rem;
  height: 100%;
  border-radius: var(--rounded-box, 1rem);
}

input[type=range][orient=vertical]::-webkit-slider-thumb {
  -webkit-appearance: none;
  border: 5px solid #000000;
  height: 24px;
  width: 24px;
  border-radius: 50%;
  background: black;
}

input[type=range][orient=vertical]::-webkit-slider-runnable-track {
  cursor: pointer;
  background: oklch(var(--p)/1);
  border-radius: var(--rounded-box, 1rem);
}
</style>