import { describe, it, expect, afterEach } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import StatusItem from '../../src/lib/components/StatusItem.svelte'
import { Health } from '../../src/lib/components/icons'

describe('StatusItem', () => {
    let component: ReturnType<typeof mount> | undefined

    afterEach(() => {
        if (component) unmount(component)
        document.body.innerHTML = ''
    })

    it('recolours its icon badge when the variant prop changes', () => {
        const props = $state({ icon: Health, title: 'Status', variant: 'success' as const })
        component = mount(StatusItem, { target: document.body, props })
        const badge = () => document.body.querySelector('.mask-hexagon')!

        expect(badge().classList).toContain('bg-success')

        props.variant = 'error' as never
        flushSync()

        expect(badge().classList).toContain('bg-error')
        expect(badge().classList).not.toContain('bg-success')
    })
})
