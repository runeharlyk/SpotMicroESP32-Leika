<script lang="ts">
	import { isConnected, data } from '../lib/socket';
	import { Icon, ArrowsPointingIn, ArrowsPointingOut, Bars3, Power } from 'svelte-hero-icons';
	import { tweened } from 'svelte/motion';
	import { quadInOut } from 'svelte/easing';
	import { sidebarOpen } from '../lib/store';

	let isFullscreen = false;

	const width = tweened(0, {
		duration: 250,
		easing: quadInOut
	});

	function handleClick() {
		if ($width === 0) width.set(75);
		else width.set(0);
	}

	const toggleFullScreen = () => {
		if (!document.fullscreenElement) document.documentElement.requestFullscreen();
		else if (document.exitFullscreen) document.exitFullscreen();
		isFullscreen = !document.fullscreenElement;
	};
</script>

<div class="absolute flex justify-between w-full z-20 h-10" on:dblclick={handleClick}>
    <div class="absolute flex justify-between w-full">
        <div class="w-20 p-4">
            <button on:click={() => sidebarOpen.set(true)}>
                <Icon src={Bars3} size="32" />
            </button>
        </div>
        <div class="w-20 p-4 text-right">{Math.floor($data[5])}°🌡️</div>
    </div>
    <div class="absolute flex justify-center w-full">
        <div>
            <div
                style="height:{$width}px; width:300px; background-color:#36393f"
                class="rounded-b-xl overflow-hidden flex justify-end flex-col"
            >
                {#if $width !== 0}
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
    </div>
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
