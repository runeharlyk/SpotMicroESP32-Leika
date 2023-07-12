<script lang="ts">
	import { Router, Route } from 'svelte-routing';
	import { onMount } from 'svelte';
	import TopBar from './components/TopBar.svelte';
	import { connect } from './lib/socket';
	import Controller from './routes/Controller.svelte';
	import Config from './routes/Config.svelte';
	import Health from './routes/SystemHealth.svelte';
	import location from './lib/location';
	import Sidebar from './components/Sidebar.svelte';

	export let url = window.location.pathname;
	onMount(() => {
		connect(`ws://${location}`);
	});
</script>

<Router {url}>
	<TopBar />
	<Sidebar />
	<div class="absolute w-full h-full">
		<Route path="/" component={Controller} />
		<Route path="/config" component={Config} />
		<Route path="/health" component={Health} />
	</div>
</Router>
