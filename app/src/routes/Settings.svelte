<script lang="ts">
	import { Link, Route, Router } from 'svelte-routing';
	import { dataBuffer, socket } from '../lib/socket';
	import { humanFileSize } from '../lib/utils';
	import Info from '../components/settings/Info.svelte';

    export const page = ""

	const menu = [
		{
			title: 'Calibration',
			path: '/calibration',
			component: Info
		},
		{
			title: 'Settings',
			path: '/settings',
			component: Info
		},
		{
			title: 'About',
			path: '/about',
			component: Info
		},
		{
			title: 'Info',
			path: '/info',
			component: Info
		}
	];
</script>

<div class="pt-14 flex h-full">
	<nav class="w-1/6 flex flex-col">
		{#each menu as link}
			<Link to={'/settings'+link.path}>
                <div class="px-4 py-2">
                    {link.title}
                    
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
