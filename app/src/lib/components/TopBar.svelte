<script lang="ts">
    import MdiHamburgerMenu from '~icons/mdi/hamburger-menu';
</script>

<div class="topbar absolute left-0 top-0 w-full z-10 flex justify-between bg-zinc-800">
	<div class="flex gap-2 p-2">
        <a href="/">
            <svelte:component this={MdiHamburgerMenu} class="h-8 w-8"/>
        </a>
    </div>
</div>

<style>
	.topbar {
		height: 50px;
	}
</style>
