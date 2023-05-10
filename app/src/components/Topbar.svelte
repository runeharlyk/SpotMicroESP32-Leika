<script lang="ts">
</script>

<div class="absolute top-0 flex justify-center w-full z-20">
	<svg height="40" width="300" class="Settings_topSVG__2VXbU">
		<path stroke="none" fill="#36393f" d="M 0 0 C 40 0 40 40 80 40 H 220 C 260 40 260 0 300 0 Z" />
	</svg>
	<div class="absolute flex gap-1 h-10 w-36 justify-center items-center dots disconnected">
		<span class="dot h-4 w-4" />
		<span class="dot h-4 w-4" />
		<span class="dot h-4 w-4" />
		<span class="dot h-4 w-4" />
	</div>
</div>

<style scoped>
	.dot {
		background-color: grey;
	}
	.disconnected .dot {
		animation: _fade 0.5s 3s infinite alternate forwards;
	}
	/*.connected .dot:first-child {
    background-color: #00bbe3;
    transform: scale(1.1);
  }*/
	/*.dots .activedot {
    background-color: #00bbe3;
    transform: scale(1.1);
  }*/
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
