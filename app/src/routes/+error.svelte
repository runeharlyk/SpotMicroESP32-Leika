<script lang="ts">
    import { page } from '$app/state'
</script>

<div class="flex justify-center items-center w-full h-full">
    <h1>{page.status} {page.error?.message}</h1>
    <span>Go to <a class="btn btn-primary" href="/">Home page</a></span>
</div>
