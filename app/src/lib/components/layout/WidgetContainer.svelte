<script lang="ts">
  import WidgetContainer from './WidgetContainer.svelte';
  import {
    WidgetComponents,
    type WidgetContainerConfig,
    isWidgetConfig,
  } from '$lib/stores/application';
  import Widget from './Widget.svelte';

  interface Props {
    container: WidgetContainerConfig;
  }

  let { container }: Props = $props();
</script>

<div class="w-full h-full flex flex-col overflow-hidden">
  <div
    class="flex w-full h-full"
    class:flex-row={container.layout === 'column'}
    class:flex-col={container.layout === 'row'}
    class:flex-wrap={container.layout === 'wrap'}>
    {#each container.widgets as widget, index (widget.id + '-' + index)}
      <Widget>
        {#if isWidgetConfig(widget)}
          {@const SvelteComponent = WidgetComponents[widget.component]}
          <SvelteComponent {...widget.props} />
        {:else if widget.widgets}
          <WidgetContainer container={widget} />
        {/if}
      </Widget>
      {#if index !== container.widgets.length - 1}
        <div
          class="divider bg-base-300 m-0"
          class:divider-horizontal={container.layout === 'column'}>
        </div>
      {/if}
    {/each}
  </div>
</div>
