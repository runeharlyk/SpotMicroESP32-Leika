<script lang="ts">
	import type { SizeUnit } from "./LayoutManager";

    export let size = -1;
    export let sizeUnit:SizeUnit = '%';
</script>

<div class="box-border overflow-hidden min-w-96 "class:flex-1={size === -1}>
    <slot></slot>
</div>
