.pragma library

function formatMs(ms) {
    if (ms <= 0) return "--:--.---"
    const m = Math.floor(ms / 60000)
    return m + ":" + String(Math.floor((ms % 60000) / 1000)).padStart(2, "0")
           + "." + String(ms % 1000).padStart(3, "0")
}
