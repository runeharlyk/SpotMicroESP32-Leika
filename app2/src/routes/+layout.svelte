<script lang="ts">
	import { page } from '$app/stores';
	import TopBar from '$lib/components/TopBar.svelte';
    import Menu from './menu.svelte';
    import '../app.css';

    let menuOpen = false;
</script>

<TopBar />

<svelte:head>
	<title>{$page.data.title}</title>
</svelte:head>

<div class="drawer lg:drawer-open">
    <input id="main-menu" type="checkbox" class="drawer-toggle" bind:checked={menuOpen} />
    <div class="drawer-content flex flex-col">
        <!-- Status bar content here -->
        <!-- <Statusbar /> -->

        <!-- Main page content here -->
        <slot />
    </div>
    <!-- Side Navigation -->
    <div class="drawer-side z-30 shadow-lg">
        <label for="main-menu" class="drawer-overlay" />
        <Menu
            on:menuClicked={() => {
                menuOpen = false;
            }}
        />
    </div>
</div>
