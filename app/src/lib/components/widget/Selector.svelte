<script lang="ts">
    export let options: string[] = [];
    export let selectedOption: string = '';
  </script>
  
  <select bind:value={selectedOption} on:change class="select select-bordered sm:select-sm max-w-xs {$$restProps.class || ''}">
    {#each options as option}
      <option value={option}>{option}</option>
    {/each}
  </select>