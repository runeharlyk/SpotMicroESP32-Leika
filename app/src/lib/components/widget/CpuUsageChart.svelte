<script lang="ts">
	import ChartBase from "./ChartBase.svelte";

    let CpuData:number[] = [1,2,3,4,5,6,7,8,9,]
</script>


<ChartBase data={CpuData} label="CPU usage" title="CPU usage"></ChartBase>