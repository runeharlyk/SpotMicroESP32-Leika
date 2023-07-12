<script lang="ts">
	import { onDestroy, onMount } from 'svelte';
	import { data, socket } from '../lib/socket';
	import { humanFileSize } from '../lib/utils';

	let buf = new Uint8Array(2);
	buf[0] = 1;

	onMount(() => {
		buf[1] = 1;
		$socket.send(buf);
	});

	onDestroy(() => {
		buf[1] = 0;
		$socket.send(buf);
	});
</script>

<div class="p-10 flex gap-4">
	<div class="bg-slate-600 rounded-md p-2 drop-shadow-lg">
		<b>Heap allocation:</b>
		<div class="flex gap-2"><span>Total free:</span>{humanFileSize($data[8])}</div>
		<div class="flex gap-2"><span>Max free block:</span>{humanFileSize($data[12])}</div>
		<div class="flex gap-2"><span>Min:</span>{humanFileSize($data[10])}</div>
	</div>

	<div class="bg-slate-600 rounded-md p-2 drop-shadow-lg">
		<b>PSRam allocation:</b>
		<div class="flex gap-2"><span>Free</span>{humanFileSize($data[9])}</div>
		<div class="flex gap-2"><span>Min:</span>{humanFileSize($data[11])}</div>
		<div class="flex gap-2"><span>Max block:</span>{humanFileSize($data[13])}</div>
	</div>
</div>
