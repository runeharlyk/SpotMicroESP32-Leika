<script lang="ts">
	import { Router, Route } from 'svelte-routing';
	import { onMount } from 'svelte';
	import TopBar from './components/TopBar.svelte';
	import socketService from '$lib/services/socket-service';
	import Controller from './routes/Controller.svelte';
	import { fileService } from '$lib/services';
	import Settings from './routes/Settings.svelte';
	import { jointNames, model, outControllerData } from '$lib/store';
	import { loadModelAsync } from '$lib/utilities';
	import { socketLocation } from '$lib/utilities';
	import type { Result } from '$lib/utilities/result';

	export let url = window.location.pathname;
	onMount(async () => {
		socketService.connect(socketLocation);
		socketService.addPublisher(outControllerData);
		registerFetchIntercept();
		const modelRes = await loadModelAsync('/spot_micro.urdf.xacro');

		if (modelRes.isOk()) {
			const [urdf, JOINT_NAME] = modelRes.inner;
			jointNames.set(JOINT_NAME);
			model.set(urdf);
		} else {
			console.error(modelRes.inner, { exception: modelRes.exception });
		}
	});

	const registerFetchIntercept = () => {
		const { fetch: originalFetch } = window;
		window.fetch = async (...args) => {
			const [resource, config] = args;
			let file: Result<Uint8Array | undefined, string>;
			file = await fileService.getFile(resource.toString());
			return file.isOk() ? new Response(file.inner) : originalFetch(resource, config);
		};
	};
</script>

<Router {url}>
	<TopBar />
	<div class="absolute w-full h-full bg-background text-on-background">
		<Route path="/" component={Controller} />
		<Route path="/settings/*page" component={Settings} />
	</div>
</Router>
