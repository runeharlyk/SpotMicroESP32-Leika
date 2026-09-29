import Visualization from '$lib/components/LazyVisualization.svelte'
import Stream from '$lib/components/Stream.svelte'
import ChartWidget from '$lib/components/widget/ChartWidget.svelte'

// Kept separate from application.ts so importing the view state does not pull in the widgets.
export const WidgetComponents = {
    Visualization,
    Stream,
    ChartWidget
}

export type WidgetComponentName = keyof typeof WidgetComponents
