<script lang="ts">
    import MdiEyeOutline from '~icons/mdi/eye-outline';
    import MdiEyeOffOutline from '~icons/mdi/eye-off-outline';

	export let show = false;
	export let value = '';
	export let id = '';
    
	$: type = show ? 'text' : 'password';

	const handleInput = (e: any) => value = e.target.value

    const togglePassword = () => show = !show
</script>

<label class="input input-bordered flex items-center gap-2">
    <input {type} class="grow" {value} on:input={handleInput} {id} />
    <!-- svelte-ignore a11y-click-events-have-key-events -->
    <div on:click={togglePassword} role="button" tabindex="0">
        <MdiEyeOffOutline class="text-base-content/50 h-6 {show ? 'block' : 'hidden'}" />
        <MdiEyeOutline class="text-base-content/50 h-6 {show ? 'hidden' : 'block'}" />
    </div>
</label>