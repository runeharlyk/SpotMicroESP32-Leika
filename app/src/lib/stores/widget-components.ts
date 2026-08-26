import Visualization from '$lib/components/Visualization.svelte'
import Stream from '$lib/components/Stream.svelte'
import ChartWidget from '$lib/components/widget/ChartWidget.svelte'

// Kept separate from application.ts so importing the view state does not pull three.js into the
// shared layout bundle.
export const WidgetComponents = {
    Visualization,
    Stream,
    ChartWidget
}

export type WidgetComponentName = keyof typeof WidgetComponents
