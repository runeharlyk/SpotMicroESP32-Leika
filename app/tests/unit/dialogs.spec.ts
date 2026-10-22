import { afterEach, describe, expect, it, vi } from 'vitest'
import { mount, unmount, type Component } from 'svelte'
import { Modals, modals, type ModalProps } from 'svelte-modals'
import ConfirmDialog from '../../src/lib/components/ConfirmDialog.svelte'
import InfoDialog from '../../src/lib/components/InfoDialog.svelte'
import NewFileDialog from '../../src/routes/system/filesystem/NewFileDialog.svelte'
import NewFolderDialog from '../../src/routes/system/filesystem/NewFolderDialog.svelte'

// Each dialog waits for its exit transition before the next one opens (exitBeforeEnter); a dialog whose exit never
// played left every later dialog closed, as the Wi-Fi page's info dialog then scan dialog did.
const DIALOGS: [string, Component<ModalProps>][] = [
    ['ConfirmDialog', ConfirmDialog],
    ['InfoDialog', InfoDialog],
    ['NewFileDialog', NewFileDialog],
    ['NewFolderDialog', NewFolderDialog]
]

describe('dialogs', () => {
    let host: ReturnType<typeof mount> | undefined

    // modals is one store for the app: the last dialog closes and finishes its exit before the next test opens one.
    afterEach(async () => {
        if (modals.stack.length) modals.closeAll()
        await vi.waitFor(() => expect(document.querySelector('[role="dialog"]')).toBeNull())
        if (host) unmount(host)
        host = undefined
        document.body.innerHTML = ''
    })

    it.each(DIALOGS)('%s closed lets the next dialog open', async (_, dialog) => {
        host = mount(Modals, { target: document.body })
        modals.open(dialog, { title: 'First', message: 'first', path: '/' })
        await vi.waitFor(() => expect(document.querySelector('[role="dialog"]')).not.toBeNull())

        modals.close()
        await vi.waitFor(() => expect(document.querySelector('[role="dialog"]')).toBeNull())
        modals.open(InfoDialog, { title: 'Second', message: 'second' })
        await vi.waitFor(() => expect(document.body.textContent).toContain('second'))
    })
})
