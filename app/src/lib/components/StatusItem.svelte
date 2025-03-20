<script lang="ts">
  type Variant = 'success' | 'error' | 'primary' | 'info' | 'warning'

  const {
    icon,
    title,
    description = '',
    variant = 'primary',
    class: klass = '',
    children = null
  } = $props<{
    icon?: any
    title: string
    description?: string | number
    variant?: Variant
    class?: string
    children?: () => any
  }>()

  const Icon = $derived(icon)

  const variants: Record<Variant, [string, string]> = {
    success: ['bg-success', 'text-success-content'],
    error: ['bg-error', 'text-error-content'],
    primary: ['bg-primary', 'text-primary-content'],
    info: ['bg-info', 'text-info-content'],
    warning: ['bg-warning', 'text-warning-content']
  }

  const variantKey: Variant = (variant as Variant) in variants ? (variant as Variant) : 'primary'
  const [bgColor, textColor] = variants[variantKey]
</script>

<div class="rounded-box bg-base-100 flex items-center space-x-3 px-4 py-2 {klass}">
  {#if icon}
    <div class="mask mask-hexagon {bgColor} h-auto w-10 flex-none">
      <Icon class="{textColor} h-auto w-full scale-75" />
    </div>
  {/if}
  <div class="grow">
    <div class="font-bold">{title}</div>
    <div class="text-sm opacity-75 grow">{description}</div>
  </div>
  {@render children?.()}
</div>
