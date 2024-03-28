<script lang="ts">
    import { goto } from '$app/navigation';
    import { onMount } from 'svelte';

    onMount(() => {
        setTimeout(() => {
            goto('/');
        }, 3000);
    });
</script>

<div class="flex justify-center items-center w-full h-full">
    <h1 class="text-4xl">404 - Page not found</h1>
    <p>You will be redirected to the home page in 3 seconds</p>
</div>