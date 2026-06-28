import { persistentStore } from '$lib/utilities'
import { get, type Writable } from 'svelte/store'
import type { WidgetComponentName } from './widget-components'

export interface WidgetConfig {
    id: string | number
    component: WidgetComponentName
    props?: Record<string, unknown>
}

export interface WidgetContainerConfig {
    id: string | number
    layout?: 'row' | 'column' | 'wrap'
    header?: string
    widgets: Array<WidgetConfig | WidgetContainerConfig>
}

export const isWidgetConfig = (
    widget: WidgetConfig | WidgetContainerConfig
): widget is WidgetConfig => 'component' in widget

interface View {
    name: string
    content: WidgetContainerConfig
}

const defaultViews: View[] = [
    {
        name: '3D representation',
        content: {
            id: 'root',
            layout: 'column',
            widgets: [{ id: 2, component: 'Visualization', props: { debug: true } }]
        }
    },
    {
        name: 'Stream',
        content: {
            id: 'root',
            layout: 'column',
            widgets: [{ id: 2, component: 'Stream' }]
        }
    },
    {
        name: 'Split screen',
        content: {
            id: 'root',
            widgets: [
                { id: 2, component: 'Stream' },
                { id: 2, component: 'Visualization', props: { debug: true } }
            ]
        }
    }
]

export const views: Writable<View[]> = persistentStore('views', defaultViews)

export const selectedView = persistentStore('selected_view', get(views)[0].name)
