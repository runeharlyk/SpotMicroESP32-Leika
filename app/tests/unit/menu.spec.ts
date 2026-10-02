import { describe, it, expect, afterEach } from 'vitest'
import { flushSync, mount, unmount } from 'svelte'
import Menu from '../../src/lib/components/menu/Menu.svelte'
import { page } from '../stubs/app-state.svelte'

describe('Menu', () => {
    let component: ReturnType<typeof mount> | undefined

    afterEach(() => {
        if (component) unmount(component)
        document.body.innerHTML = ''
    })

    const link = (href: string) => document.body.querySelector(`a[href="${href}"]`)!

    it('highlights the entry for the current path even when the page sets no title', () => {
        page.url = new URL('http://localhost/wifi/mdns')
        page.data = {}
        component = mount(Menu, { target: document.body, props: { menuClicked: () => {} } })
        flushSync()

        expect(link('/wifi/mdns').classList).toContain('bg-base-100')
        expect(link('/wifi/sta').classList).not.toContain('bg-base-100')
    })

    it('does not highlight an entry that merely shares the current page title', () => {
        page.url = new URL('http://localhost/wifi/sta')
        page.data = { title: 'Controller' }
        component = mount(Menu, { target: document.body, props: { menuClicked: () => {} } })
        flushSync()

        expect(link('/controller').classList).not.toContain('bg-base-100')
        expect(link('/wifi/sta').classList).toContain('bg-base-100')
    })
})
