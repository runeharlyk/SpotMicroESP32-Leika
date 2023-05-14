<script lang="ts">
	import { Icon, XMark, ChartBar, DevicePhoneMobile, Cog6Tooth } from 'svelte-hero-icons';
	import { sidebarOpen } from '../lib/store';

	const closeMenu = () => {
		sidebarOpen.set(false);
	};
</script>

<div
	id="drawer-example"
	style="background-color:#36393f"
	class="fixed top-0 left-0 z-40 h-screen p-4 overflow-y-auto transition-transform
    {$sidebarOpen ? '' : '-translate-x-full'} bg-white w-40 dark:bg-gray-800"
	tabindex="-1"
	aria-labelledby="drawer-label"
>
	<button
		type="button"
		on:click={closeMenu}
		class="text-gray-400 bg-transparent hover:bg-gray-200 hover:text-gray-900 rounded-lg text-sm p-1.5 absolute top-2.5 right-2.5 inline-flex items-center dark:hover:bg-gray-600 dark:hover:text-white"
	>
		<Icon src={XMark} size="32" />
	</button>
	<div class="h-full w-full flex flex-col justify-evenly items-center">
		<button on:click={closeMenu}><Icon src={DevicePhoneMobile} size="32" /></button>
		<button on:click={closeMenu}><Icon src={ChartBar} size="32" /></button>
		<button on:click={closeMenu}><Icon src={Cog6Tooth} size="32" /></button>
	</div>
</div>
