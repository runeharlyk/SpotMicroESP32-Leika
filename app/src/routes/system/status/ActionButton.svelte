<script>
  const { icon, type, label, ...rest } = $props()

  const Icon = $derived(icon)
</script>

<button class="btn btn-{type} inline-flex items-center" {...rest}>
  <Icon class="mr-2 h-5 w-5" />
  <span>{label}</span>
</button>
