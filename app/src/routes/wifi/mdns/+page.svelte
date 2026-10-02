<script lang="ts">
    import RequiresRobot from '$lib/components/RequiresRobot.svelte'
    import MDNS from './MDNS.svelte'
</script>

<div class="mx-0 my-1 flex flex-col space-y-4 sm:mx-8 sm:my-8">
    <RequiresRobot>
        <MDNS />
    </RequiresRobot>
</div>
