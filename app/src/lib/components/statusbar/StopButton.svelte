<script lang="ts">
	import { mode, modes } from "$lib/stores";

    const deactivate = async () => {
		mode.set(modes.indexOf('deactivated'));
	};
</script>


<button onclick={deactivate} class="bg-error text-white btn rounded-none">STOP</button>