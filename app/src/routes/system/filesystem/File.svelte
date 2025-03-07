<script>
  import { FileIcon } from '$lib/components/icons'

  let { name, selected } = $props()
</script>

<!-- svelte-ignore a11y_interactive_supports_focus -->
<!-- svelte-ignore a11y_click_events_have_key_events -->
<span role="button" class="flex pl-4 gap-2 items-center" onclick={selected}>
  <FileIcon />{name}
</span>
