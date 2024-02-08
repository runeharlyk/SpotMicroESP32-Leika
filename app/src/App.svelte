<script lang="ts">
	import { Router, Route } from 'svelte-routing';
	import { onMount } from 'svelte';
	import TopBar from './components/TopBar.svelte';
	import { connect } from './lib/socket';
	import Controller from './routes/Controller.svelte';
    import FileCache from './lib/cache';
	import { socketLocation } from './lib/location';
    import Settings from './routes/Settings.svelte';
	import { jointNames, model } from './lib/store';
	import { loadModelAsync } from './lib/modelLoader';

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
            await FileCache.openDatabase();
            let file: BodyInit | Uint8Array | undefined | null;
            try {
                file = await FileCache.getFile(resource.toString());
            } catch (error) {
                console.log(error);
            }

            return file 
            ? new Response(file)
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
