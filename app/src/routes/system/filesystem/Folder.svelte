<script lang="ts">
  import Folder from './Folder.svelte'
  import File from './File.svelte'
  import { FolderIcon, FolderOpenOutline } from '$lib/components/icons'

  interface Props {
    expanded?: boolean
    name: string
    files: any
    selected: (name: string) => void
    onDelete: (name: string) => void
  }

  let { expanded = $bindable(false), name, files, selected, onDelete }: Props = $props()

  function toggle() {
    expanded = !expanded
  }
</script>

<div class="folder-item">
  <button class="flex items-center pl-2 hover:bg-gray-700 w-full rounded py-1" onclick={toggle}>
    {#if expanded}
      <FolderOpenOutline class="w-5 h-5 mr-1" />
    {:else}
      <FolderIcon class="w-5 h-5 mr-1" />
    {/if}
    <span class="text-sm">{name}</span>
  </button>

  {#if expanded}
    <ul class="ml-4 border-l border-gray-600 mt-1">
      {#each Object.entries(files) as [itemName, content]}
        <li class="py-1">
          {#if typeof content === 'object'}
            <Folder name={itemName} files={content} {selected} {onDelete} />
          {:else}
            <File name={itemName} {selected} {onDelete} />
          {/if}
        </li>
      {/each}
    </ul>
  {/if}
</div>
