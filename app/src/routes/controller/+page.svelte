<script lang="ts">
    import Visualization from "$lib/components/Visualization.svelte";
    import Lidar from "$lib/components/Lidar.svelte";
</script>

<div class="grow flex">
    <div class="absolute h-screen w-full top-0 flex">
        <div class="flex-1 overflow-hidden">
            <Visualization debug />
        </div>
        <div class="flex-1">
            <Lidar />
        </div>
    </div>
</div> 