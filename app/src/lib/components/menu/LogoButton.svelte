<script>
	import logo from '$lib/assets/logo512.png';

	/** @type {{appName: any}} */
	let { appName } = $props();
</script>

<a
	href="/"
	class="rounded-box mb-4 flex items-center hover:scale-[1.02] active:scale-[0.98]"
>
	<img src={logo} alt="Logo" class="h-12 w-12" />
	<h1 class="px-4 text-2xl font-bold">{appName}</h1>
</a>
