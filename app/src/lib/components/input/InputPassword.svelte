<script lang="ts">
    import { MdiEyeOffOutline, MdiEyeOutline } from '../icons'

    interface Props {
        show?: boolean
        value?: string
        id?: string
    }

    let { show = $bindable(false), value = $bindable(''), id = '' }: Props = $props()

    let type = $derived(show ? 'text' : 'password')

    const handleInput = (e: Event) => (value = (e.target as HTMLInputElement).value)

    const togglePassword = () => (show = !show)
</script>

<label class="input input-bordered flex items-center gap-2">
    <input {type} class="grow" {value} oninput={handleInput} {id} />
    <button
        type="button"
        onclick={togglePassword}
        aria-label={show ? 'Hide password' : 'Show password'}
        aria-pressed={show}
    >
        <MdiEyeOffOutline class="text-base-content/50 h-6 {show ? 'block' : 'hidden'}" />
        <MdiEyeOutline class="text-base-content/50 h-6 {show ? 'hidden' : 'block'}" />
    </button>
</label>
