<script lang="ts">
    import type { servo } from "$lib/models";
    export let servo: servo;
</script>

<div>
    <h2 class="text-lg">{ servo.name }</h2>
    <div class="flex gap-2 items-center">
        Is inverted <input type="checkbox" checked={servo.inverted} class="checkbox"/>
    </div>
    <div class="relative mb-6">
        <label for="labels-range-input" class="sr-only">Labels range</label>
        <input id="labels-range-input" type="range" value="1000" min="100" max="1500" class="w-full h-2 bg-gray-200 rounded-lg appearance-none cursor-pointer dark:bg-gray-700">
        <span class="text-sm text-gray-500 dark:text-gray-400 absolute start-0 -bottom-6">Min ($100)</span>
        <span class="text-sm text-gray-500 dark:text-gray-400 absolute start-1/3 -translate-x-1/2 rtl:translate-x-1/2 -bottom-6">$500</span>
        <span class="text-sm text-gray-500 dark:text-gray-400 absolute start-2/3 -translate-x-1/2 rtl:translate-x-1/2 -bottom-6">$1000</span>
        <span class="text-sm text-gray-500 dark:text-gray-400 absolute end-0 -bottom-6">Max ($1500)</span>
    </div>
    <button class="btn btn-ghost rounded-btn">Sweep range</button>
</div>