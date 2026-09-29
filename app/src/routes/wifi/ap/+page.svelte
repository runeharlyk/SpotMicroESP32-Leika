<script lang="ts">
    import RequiresHttpApi from '$lib/components/RequiresHttpApi.svelte'
    import Accesspoint from './Accesspoint.svelte'
</script>

<div class="mx-0 my-1 flex flex-col space-y-4 sm:mx-8 sm:my-8">
    <RequiresHttpApi>
        <Accesspoint />
    </RequiresHttpApi>
</div>
