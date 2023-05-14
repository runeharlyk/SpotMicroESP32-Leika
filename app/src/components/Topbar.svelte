<script lang="ts">
	import { isConnected, data } from '../lib/socket';
	import {
		Icon,
		ArrowsPointingIn,
		ArrowsPointingOut,
		Sparkles,
		Bars3,
		Power
	} from 'svelte-hero-icons';
	import { tweened } from 'svelte/motion';
	import { quadInOut } from 'svelte/easing';
	import { sidebarOpen } from '../lib/store';
	import { humanFileSize } from '../lib/utils';

	let isFullscreen = false;

	const width = tweened(0, {
		duration: 200,
		easing: quadInOut
	});

	function handleClick() {
		if ($width === 0) width.set(200);
		else width.set(0);
		console.log('clicked');
	}

	const toggleFullScreen = () => {
		if (!document.fullscreenElement) document.documentElement.requestFullscreen();
		else if (document.exitFullscreen) document.exitFullscreen();
		isFullscreen = !document.fullscreenElement;
	};

	const round = (val: number) => {
		return (Math.round(val * 100) / 100).toFixed(2);
	};
</script>

<div class="absolute flex justify-between w-full z-20 h-10" on:dblclick={handleClick}>
	<div class="w-20 p-4">
		<button on:click={() => sidebarOpen.set(true)}>
			<Icon src={Bars3} size="32" />
		</button>
	</div>
	<div>
		<div
			style="height:{$width}px; width:450px; background-color:#36393f"
			class="rounded-b-xl overflow-hidden flex justify-end flex-col"
		>
			{#if $width !== 0}
				<div class="px-4 grid grid-cols-3 w-full">
					<div class="flex gap-2"><span>RSSI:</span>{$data[0]}db</div>
					<div class="flex gap-2"><span>MPU:</span>{round($data[1])}°</div>
					<div class="flex gap-2"><span>CPU:</span>{round($data[8])}°</div>
					<div class="flex gap-2"><span>X:</span>{round($data[5])}</div>
					<div class="flex gap-2"><span>Y:</span>{round($data[6])}</div>
					<div class="flex gap-2"><span>Z:</span>{round($data[7])}</div>
					<div class="flex gap-2"><span>Left:</span>{round($data[9])}cm</div>
					<div class="flex gap-2"><span>Right:</span>{round($data[10])}cm</div>
					<div class="flex gap-2"><span>Heap:</span>{humanFileSize($data[11])}</div>
					<div class="flex gap-2"><span>Psram:</span>{humanFileSize($data[12])}</div>
				</div>
				<div class="flex justify-evenly p-4 w-full">
					<button on:click={toggleFullScreen}>
						<Icon src={isFullscreen ? ArrowsPointingIn : ArrowsPointingOut} size="32" />
					</button>
					<button>
						<Icon src={Power} size="32" />
					</button>
				</div>
			{/if}
		</div>
		<div class="flex justify-center" on:mouseup={handleClick}>
			<svg height="40" width="300" class="Settings_topSVG__2VXbU">
				<path
					stroke="none"
					fill="#36393f"
					d="M 0 0 C 40 0 40 40 80 40 H 220 C 260 40 260 0 300 0 Z"
				/>
			</svg>
			<div
				class="absolute flex gap-1 h-10 w-36 justify-center items-center dots
			{$isConnected ? 'connected' : 'disconnected'}"
			>
				<span class="dot h-4 w-4" />
				<span class="dot h-4 w-4" />
				<span class="dot h-4 w-4" />
				<span class="dot h-4 w-4" />
			</div>
		</div>
	</div>
	<div class="w-20 p-4 text-right">{Math.floor($data[8])}°🌡️</div>
</div>

<style scoped>
	.dot {
		background-color: grey;
	}
	.disconnected .dot {
		animation: _fade 0.5s 3s infinite alternate forwards;
	}
	.connected .dot:first-child {
		background-color: #00bbe3;
		transform: scale(1.1);
	}
	.dots .dot:first-child {
		animation-delay: 0.25s;
	}
	.dots .dot:nth-child(2) {
		animation-delay: 0.5s;
	}
	.dots .dot:nth-child(3) {
		animation-delay: 0.75s;
	}
	.dots .dot:last-child {
		animation-delay: 1s;
	}
	.dots .dot:last-child {
		animation-delay: 1s;
	}
	@keyframes _fade {
		from {
			background-color: #00bbe3;
			transform: scale(1.1);
		}
		to {
			background-color: grey;
			transform: scale(1.1);
		}
	}
</style>
