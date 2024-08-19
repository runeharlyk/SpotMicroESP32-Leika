<script lang="ts">
	import { selectedView, views } from "$lib/stores/application";
	import Selector from "../widget/Selector.svelte";
</script>

<Selector bind:selectedOption={$selectedView} options={$views.map((v) => v.name)} />