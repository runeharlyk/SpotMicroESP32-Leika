<script lang="ts">
	import { onDestroy, onMount } from 'svelte';
	import { dataBuffer, socket } from '../lib/socket';
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
		<div class="flex gap-2"><span>Total free:</span>{humanFileSize($dataBuffer[8])}</div>
		<div class="flex gap-2"><span>Max free block:</span>{humanFileSize($dataBuffer[12])}</div>
		<div class="flex gap-2"><span>Min:</span>{humanFileSize($dataBuffer[10])}</div>
	</div>

	<div class="bg-slate-600 rounded-md p-2 drop-shadow-lg">
		<b>PSRam allocation:</b>
		<div class="flex gap-2"><span>Free</span>{humanFileSize($dataBuffer[9])}</div>
		<div class="flex gap-2"><span>Min:</span>{humanFileSize($dataBuffer[11])}</div>
		<div class="flex gap-2"><span>Max block:</span>{humanFileSize($dataBuffer[13])}</div>
	</div>
</div>
