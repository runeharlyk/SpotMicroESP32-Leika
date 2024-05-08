<script lang="ts">
    import { user } from '$lib/stores/user';
	import SettingsCard from "$lib/components/SettingsCard.svelte";
    import Camera from '~icons/mdi/camera-outline'
    import VideoCamera from '~icons/mdi/videocam-outline'
    import Reload from '~icons/mdi/reload'
    import Record from '~icons/mdi/radio-button-unchecked'
    import Recording from '~icons/mdi/radio-button-checked'
	
	import Spinner from '$lib/components/Spinner.svelte';
	import CameraSetting from './CameraSetting.svelte';
	import { onDestroy, onMount } from 'svelte';

    const ws_token = `?access_token=${$user.bearer_token}`

    let stillId = 0 

    let recording = false

    let videoMode = false

    const takeStill = () => stillId += 1

    const toggleMode = () => {
        videoMode = !videoMode
    }

    let timer:number

    onMount(() => {
        timer = setInterval(takeStill, 1000)
    })

    onDestroy(() => {
        clearInterval(timer)
    })
</script>


<SettingsCard collapsible={false}>
    <Camera slot="icon" class="lex-shrink-0 mr-2 h-6 w-6 self-end" />
    <span slot="title">Camera</span>
    <img src={"/api/camera/still"+ ws_token + "key=" + stillId} alt="Live-stream" class="w-full rounded-lg shadow-lg" />
    <CameraSetting />
    <div>
        <!-- <div class="relative">
            <div class="-top-12 absolute flex justify-center w-full">
                <label class="swap">
                    <input type="checkbox" bind:value={recording} />
                    <div class="swap-on"><Recording class="h-10 w-10" /></div>
                    <div class="swap-off"><Record class="h-10 w-10" /></div>
                  </label>
            </div>
            <div class="flex justify-center">
                <div class="flex justify-center gap-4 p-2 rounded-xl bg-opacity-50 bg-slate-600 mt-2">
                    <button class="btn-outline" class:btn-primary={!videoMode} on:click={toggleMode}><Camera class="h-5 w-5" /></button>
                    <button class="btn-outline" class:btn-primary={ videoMode} on:click={toggleMode}><VideoCamera class="h-5 w-5" /></button>
                </div>
            </div>
        </div> -->
    </div>
    
    <!-- <button class="btn btn-primary inline-flex items-center" on:click={takeStill}>
		<Reload class="mr-2 h-5 w-5" />
		<span>Reload</span>
	</button> -->
</SettingsCard>