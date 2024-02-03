<script lang="ts">
	import { isConnected, dataBuffer, status } from '../lib/socket';
	import { Icon, Bars3, Power, Battery100, Signal, SignalSlash } from 'svelte-hero-icons';
	import { emulateModel, sidebarOpen } from '../lib/store';

    const views = ["Robot camera", "Virtual environment"]
    const modes = ["Drive", "Choreography"]

</script>


<div class="topbar absolute left-0 top-0 w-full z-10 flex justify-between bg-zinc-800">
    <div class="flex gap-2 py-2">
    <button class="ml-2" on:click={() => sidebarOpen.set(true)}>
        <Icon src={Bars3} size="32" />
    </button>
        <select>
            {#each modes as mode}
            <option>{mode}</option>
            {/each}
        </select>
        
        <select>
            {#each views as view}
            <option>{view}</option>
            {/each}
        </select>
    </div>

    <div class="flex gap-2 p-2">
        <button class="action_button bg-zinc-600">
            <Icon src={Power} size="24" />
        </button>
        <button class="action_button"><Icon src={Battery100} size="24" /></button>
        <button class="action_button"><Icon src={$isConnected ? Signal : SignalSlash} size="24" /></button>
    </div>
    <div>
        <button class="h-full w-20 bg-red-600 text-white">STOP</button>
    </div>
</div>

<style>
    .topbar {
        height: 50px;
    }
    .action_button {
        border-radius: 4px;
        width: 34px;
        height: 34px;
        display: flex;
        justify-content: center;
        align-items: center;
        outline: 1px solid #52525b;
    }
</style>