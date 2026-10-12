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
            widgets: [{ id: 2, component: 'Visualization' }]
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
                { id: 2, component: 'Visualization' }
            ]
        }
    },
    {
        name: 'Simulation',
        content: {
            id: 'root',
            layout: 'column',
            widgets: [{ id: 3, component: 'Simulation' }]
        }
    },
    {
        // The robot's reported pose beside the simulation, both driven by the same sticks: how far apart they move.
        name: 'Robot and simulation',
        content: {
            id: 'root',
            layout: 'column',
            widgets: [
                { id: 2, component: 'Visualization' },
                { id: 3, component: 'Simulation' }
            ]
        }
    }
]

export const views: Writable<View[]> = persistentStore('views', defaultViews)

// Views persist per browser, so a view list saved before a view was added gains it here.
views.update(list => [
    ...list,
    ...defaultViews.filter(view => !list.some(saved => saved.name === view.name))
])

export const selectedView = persistentStore('selected_view', get(views)[0].name)

// Without a robot there is nothing to show but the simulation, which can be played with; a view
// picked then is remembered apart from the one picked for a robot.
export const offlineView = persistentStore('selected_view_offline', 'Simulation')
