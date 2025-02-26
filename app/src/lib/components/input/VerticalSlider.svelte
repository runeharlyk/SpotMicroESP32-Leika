<script lang="ts">
  import { createBubbler } from 'svelte/legacy';

  const bubble = createBubbler();
  interface Props {
    min?: number;
    max?: number;
    step?: number;
    value?: any;
  }

  let {
    min = 0,
    max = 100,
    step = 1,
    value = $bindable((max - min) / 2)
  }: Props = $props();
</script>
  
<input
    type="range"
    style="writing-mode: vertical-lr; direction: rtl"
    class="cursor-pointer"
    min={min}
    max={max}
    step={step}
    bind:value
    oninput={bubble('input')}
    onchange={bubble('change')}
/>

<style>
input[type=range]::-webkit-slider-runnable-track {
  background: oklch(var(--p)/1);
  border-radius: var(--rounded-box, 1rem);
} 
</style>