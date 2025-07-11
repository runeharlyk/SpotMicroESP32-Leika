#pragma once

enum class StateUpdateResult {
    CHANGED = 0, // The update changed the state and propagation should take place if required
    UNCHANGED,   // The state was unchanged, propagation should not take place
    ERROR        // There was a problem updating the state, propagation should not take place
};