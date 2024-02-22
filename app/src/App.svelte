<script lang="ts">
	import { Router, Route } from 'svelte-routing';
	import { onMount } from 'svelte';
	import TopBar from './components/TopBar.svelte';
	import { connect } from '$lib/socket';
	import Controller from './routes/Controller.svelte';
    import FileService from '$lib/services/file-service';
    import Settings from './routes/Settings.svelte';
	import { jointNames, model } from '$lib/store';
	import { loadModelAsync } from '$lib/utilities';
	import { socketLocation } from '$lib/utilities';
	import type { Result } from '$lib/utilities/result';

	export let url = window.location.pathname 
	onMount(async () => {
		connect(socketLocation);
        registerFetchIntercept()
        const [urdf, JOINT_NAME] = await loadModelAsync('/spot_micro.urdf.xacro') 
        jointNames.set(JOINT_NAME)
        model.set(urdf)
	});

    const registerFetchIntercept = () => {
        const { fetch: originalFetch } = window;
        window.fetch = async (...args) => {
            const [resource, config] = args;
            let file: Result<Uint8Array | undefined, string>;
            file = await FileService.getFile(resource.toString());
            return file.isOk() 
            ? new Response(file.inner)
            : originalFetch(resource, config) 
        };
    }
</script>

<Router {url}>
	<TopBar />
	<div class="absolute w-full h-full bg-background text-on-background">
		<Route path="/" component={Controller} />
		<Route path="/settings/*page" component={Settings} />
	</div>
</Router>
