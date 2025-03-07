<script lang="ts">
  interface Props {
    min?: number
    max?: number
    step?: number
    value?: any
    oninput?: any
  }

  let {
    min = 0,
    max = 100,
    step = 1,
    value = $bindable((max - min) / 2),
    ...rest
  }: Props = $props()
</script>

<input
  type="range"
  style="writing-mode: vertical-lr; direction: rtl"
  class="cursor-pointer"
  {min}
  {max}
  {step}
  bind:value
  {...rest} />

<style>
  input[type='range']::-webkit-slider-runnable-track {
    background: oklch(var(--p) / 1);
    border-radius: var(--rounded-box, 1rem);
  }
</style>
