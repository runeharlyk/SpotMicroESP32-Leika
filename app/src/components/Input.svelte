<script lang="ts">
    export let value
    export let placeholder = ""
    export let required = false
    export let disabled = false
</script>
<div  class="{$$restProps.class || ''}">
    <label for="first_name" class="block text-sm font-medium text-gray-900 dark:text-white"><slot/></label>
    <input type="number" on:change placeholder={placeholder} disabled={disabled} bind:value={value} required={required} class="bg-gray-50 border border-gray-300 text-gray-900 text-sm rounded-lg focus:ring-blue-500 focus:border-blue-500 block w-full p-1 dark:bg-gray-700 dark:border-gray-600 dark:placeholder-gray-400 dark:text-white dark:focus:ring-blue-500 dark:focus:border-blue-500">
</div>