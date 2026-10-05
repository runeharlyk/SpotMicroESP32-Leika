<script lang="ts">
    // Stands in for the WebGL preview, which jsdom cannot draw: it shows which legs got handles.
    const { handles = [] }: { handles?: { leg: number }[] } = $props()
</script>

<div data-testid="preview" data-handles={handles.map(h => h.leg).join(',')}></div>
