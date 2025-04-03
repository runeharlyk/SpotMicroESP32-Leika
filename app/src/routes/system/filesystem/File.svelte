<script lang="ts">
  import { FileIcon, TrashIcon } from '$lib/components/icons'

  interface Props {
    name: string
    selected: (name: string) => void
    onDelete: (name: string) => void
  }

  let { name, selected, onDelete }: Props = $props()
</script>

<div class="flex items-center pl-4 group hover:bg-gray-700 rounded py-1">
  <button class="flex items-center gap-2 flex-grow" onclick={() => selected(name)}>
    <FileIcon class="w-4 h-4" />
    <span class="text-sm">{name}</span>
  </button>

  <button
    class="opacity-0 group-hover:opacity-100 p-1 hover:text-red-500"
    onclick={() => onDelete(name)}>
    <TrashIcon class="w-4 h-4" />
  </button>
</div>
