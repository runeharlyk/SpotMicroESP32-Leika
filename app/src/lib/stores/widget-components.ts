import Visualization from '$lib/components/Visualization.svelte'
import Stream from '$lib/components/Stream.svelte'
import ChartWidget from '$lib/components/widget/ChartWidget.svelte'

// Maps widget config names to their components. Kept separate from application.ts (which holds the
// views/selectedView state) so that importing the view state — e.g. from the always-rendered
// status-bar ViewSelector — does NOT pull these heavy components (three.js via Visualization) into
// the shared layout bundle. Only WidgetContainer, which actually renders widgets, imports this.
export const WidgetComponents = {
    Visualization,
    Stream,
    ChartWidget
}

export type WidgetComponentName = keyof typeof WidgetComponents
