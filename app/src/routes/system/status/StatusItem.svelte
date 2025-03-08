<script lang="ts">
  const { icon, title, description } = $props()

  const Icon = $derived(icon)
</script>

<div class="rounded-box bg-base-100 flex items-center space-x-3 px-4 py-2">
  <div class="mask mask-hexagon bg-primary h-auto w-10 flex-none">
    <Icon class="text-primary-content h-auto w-full scale-75" />
  </div>
  <div>
    <div class="font-bold">{title}</div>
    <div class="text-sm opacity-75">{description}</div>
  </div>
</div>
