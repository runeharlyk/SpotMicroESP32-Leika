<script lang="ts">
  import MenuList from './MenuList.svelte'
  type MenuItem = {
    title: string
    icon: ConstructorOfATypedSvelteComponent
    href?: string
    feature: boolean
    active?: boolean
    submenu?: MenuItem[]
  }

  let { level, menuItems, select, class: klass } = $props()

  const selectMenuItem = (title: string) => {
    select(title)
  }
</script>

<ul class={klass + ' menu w-full'}>
  {#each menuItems as MenuItem[] as menuItem, i (menuItem.title)}
    {#if menuItem.feature}
      <li>
        {#if menuItem.submenu}
          <details open={menuItem.submenu.some(subItem => subItem.active)}>
            <summary class="font-bold">
              <menuItem.icon class="h-6 w-6" />
              {menuItem.title}
            </summary>
            <div class="pl-4">
              <MenuList menuItems={menuItem.submenu} level={level + 1} {select} class={klass} />
            </div>
          </details>
        {:else}
          <a
            href={menuItem.href}
            class="font-bold"
            class:bg-base-100={menuItem.active}
            class:text-lg={level === 0}
            class:text-md={level === 1}
            onclick={() => selectMenuItem(menuItem.title)}>
            <menuItem.icon class="h-6 w-6" />
            {menuItem.title}
          </a>
        {/if}
      </li>
    {/if}
  {/each}
</ul>
