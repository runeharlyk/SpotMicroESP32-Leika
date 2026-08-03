<script lang="ts">
    import { onMount } from 'svelte';
    import { ModeData, ModesEnum } from '$lib/platform_shared/message';
    import { mode } from '$lib/stores';

    const deactivate = async () => {
        mode.set(ModeData.create({ mode: ModesEnum.DEACTIVATED }));
    };

    onMount(() => {
        const handleKeyDown = (event: KeyboardEvent) => {
            if (event.code !== 'Space') return;

            const target = event.target as HTMLElement | null;

            // Don't trigger if typing in a text field
            if (
                target &&
                (
                    target.tagName === 'INPUT' ||
                    target.tagName === 'TEXTAREA' ||
                    target.tagName === 'SELECT' ||
                    target.isContentEditable
                )
            ) {
                return;
            }

            event.preventDefault();
            deactivate();
        };

        window.addEventListener('keydown', handleKeyDown);

        return () => {
            window.removeEventListener('keydown', handleKeyDown);
        };
    });
</script>

<button onclick={deactivate} class="bg-error text-white btn rounded-none">
    STOP
</button>
