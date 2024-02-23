<script lang="ts">
	import { Link, Route, Router } from 'svelte-routing';
	import Info from '../components/settings/Info.svelte';
	import Log from '../components/settings/Log.svelte';
	import Configuration from '../components/settings/Configuration.svelte';
	import {
		Icon,
		Wifi,
		CommandLine,
		InformationCircle,
		BookOpen,
		AdjustmentsVertical,
		Cog6Tooth,
		Newspaper
	} from 'svelte-hero-icons';
	import Calibration from '../components/settings/Calibration.svelte';

	export const page = '';

	const menu = [
		{
			title: 'Calibration',
			path: '/calibration',
			icon: AdjustmentsVertical,
			component: Calibration
		},
		{
			title: 'System info',
			path: '/info',
			icon: InformationCircle,
			component: Info
		},
		{
			title: 'Log',
			path: '/log',
			icon: BookOpen,
			component: Log
		},
		{
			title: 'Settings',
			path: '/settings',
			icon: Cog6Tooth,
			component: Configuration
		}
	];
</script>

<div class="pt-14 flex h-full">
	<nav class="w-1/6 flex flex-col">
		{#each menu as link}
			<Link to={'/settings' + link.path}>
				<div class="px-4 py-2 flex gap-2 items-center">
					<Icon src={link.icon} size="24" />{link.title}
				</div>
			</Link>
		{/each}
	</nav>
	<main class="w-full h-full">
		<Router>
			{#each menu as link}
				<Route path={link.path} component={link.component}></Route>
			{/each}
		</Router>
	</main>
</div>
