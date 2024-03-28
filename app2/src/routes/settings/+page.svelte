<!-- <script lang="ts">
	import Info from '$lib/components/settings/Info.svelte';
	import Log from '$lib/components/settings/Log.svelte';
	import Configuration from '$lib/components/settings/Configuration.svelte';
	import Calibration from '$lib/components/settings/Calibration.svelte';

	export const page = '';

	const menu = [
		{
			title: 'Calibration',
			path: '/calibration',
			// icon: AdjustmentsVertical,
			component: Calibration
		},
		{
			title: 'System info',
			path: '/info',
			// icon: InformationCircle,
			component: Info
		},
		{
			title: 'Log',
			path: '/log',
			// icon: BookOpen,
			component: Log
		},
		{
			title: 'Settings',
			path: '/settings',
			// icon: Cog6Tooth,
			component: Configuration
		}
	];
</script> -->

<h1 class="text-2xl font-bold">Settings</h1>

<div class="pt-14 flex h-full">
	<nav class="w-1/6 flex flex-col">
		<!-- {#each menu as link} -->
			<!-- <Link to={'/settings' + link.path}> -->
				<div class="px-4 py-2 flex gap-2 items-center">
					<!-- <Icon src={link.icon} size="24" />{link.title} -->
				</div>
			<!-- </Link> -->
		<!-- {/each} -->
	</nav>
	<main class="w-full h-full">
		<!-- <Router> -->
			<!-- {#each menu as link} -->
				<!-- <Route path={link.path} component={link.component}></Route> -->
			<!-- {/each} -->
		<!-- </Router> -->
	</main>
</div>
