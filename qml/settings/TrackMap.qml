import QtQuick 2.15

Item {
    id: root

    // Flat [lat, lon, lat, lon, …] list for the lap being displayed
    property var path: []

    // Finish-line gate endpoints A→B for the displayed lap (from TrackModel.finishLineFor)
    property real finishLat1: 0
    property real finishLon1: 0
    property real finishLat2: 0
    property real finishLon2: 0

    readonly property bool hasPath: path.length >= 4
    readonly property bool hasFinish: (finishLat1 !== 0 || finishLon1 !== 0)
                                   && (finishLat2 !== 0 || finishLon2 !== 0)

    // Zoom/pan state, confined to this map container. zoomLevel multiplies the
    // fit-to-bounds scale computed in onPaint; panX/panY are pixel offsets.
    property real zoomLevel: 1.0
    property real panX: 0
    property real panY: 0
    readonly property real minZoom: 1.0
    readonly property real maxZoom: 8.0

    // Cached fit-to-view metrics from the last paint (draw size at zoomLevel
    // 1, and the available canvas size) — lets pan/zoom handlers clamp
    // without re-deriving the lat/lon bounding box outside onPaint.
    property real _fitDrawW: 0
    property real _fitDrawH: 0
    property real _availW: 0
    property real _availH: 0

    // Cached lat/lon projection bounds (and longitude correction). These depend
    // only on `path` and the finish-gate endpoints, so they're rebuilt lazily by
    // recomputeBounds() only when those change — never on the per-frame repaints
    // that pan/zoom trigger.
    property real _minLat: 0
    property real _minLon: 0
    property real _kx: 1
    property real _spanX: 1e-9
    property real _spanY: 1e-9
    property bool _boundsValid: false

    // Keeps panX/panY within range so the track can't be dragged fully out
    // of view; at zoomLevel 1 (draw size == fit size) this clamps to 0.
    function clampPan() {
        const drawW = root._fitDrawW * root.zoomLevel
        const drawH = root._fitDrawH * root.zoomLevel
        const maxPanX = Math.max(0, (drawW - root._availW) / 2)
        const maxPanY = Math.max(0, (drawH - root._availH) / 2)
        root.panX = Math.max(-maxPanX, Math.min(maxPanX, root.panX))
        root.panY = Math.max(-maxPanY, Math.min(maxPanY, root.panY))
    }

    // Zooms by `factor` anchored at canvas-relative point (cx, cy) so that
    // point stays fixed on screen (standard "zoom toward cursor" formula).
    function applyZoom(factor, cx, cy) {
        const oldZoom = root.zoomLevel
        const newZoom = Math.max(root.minZoom, Math.min(root.maxZoom, oldZoom * factor))
        if (newZoom === oldZoom) return
        const ratio = newZoom / oldZoom
        const cxCenter = cx - canvas.width / 2
        const cyCenter = cy - canvas.height / 2
        root.panX = (root.panX - cxCenter) * ratio + cxCenter
        root.panY = (root.panY - cyCenter) * ratio + cyCenter
        root.zoomLevel = newZoom
        root.clampPan()
    }

    function resetView() {
        root.zoomLevel = 1.0
        root.panX = 0
        root.panY = 0
    }

    onZoomLevelChanged: canvas.requestPaint()
    onPanXChanged: canvas.requestPaint()
    onPanYChanged: canvas.requestPaint()

    // Rebuilds the cached lat/lon bounding box (and longitude correction) from
    // the current path and finish gate. Called lazily from onPaint when the
    // cache has been invalidated, so the O(n) scan runs once per lap/finish
    // change rather than once per repaint.
    function recomputeBounds() {
        if (!root.hasPath) {
            root._boundsValid = false
            return
        }
        let minLat = root.path[0], maxLat = root.path[0]
        let minLon = root.path[1], maxLon = root.path[1]
        for (let i = 0; i < root.path.length; i += 2) {
            const lat = root.path[i], lon = root.path[i + 1]
            if (lat < minLat) minLat = lat
            if (lat > maxLat) maxLat = lat
            if (lon < minLon) minLon = lon
            if (lon > maxLon) maxLon = lon
        }
        // Include the finish gate so it's never clipped outside the map
        if (root.hasFinish) {
            minLat = Math.min(minLat, root.finishLat1, root.finishLat2)
            maxLat = Math.max(maxLat, root.finishLat1, root.finishLat2)
            minLon = Math.min(minLon, root.finishLon1, root.finishLon2)
            maxLon = Math.max(maxLon, root.finishLon1, root.finishLon2)
        }
        const midLat = (minLat + maxLat) / 2.0
        const kx = Math.cos(midLat * Math.PI / 180.0)
        root._minLat = minLat
        root._minLon = minLon
        root._kx = kx
        // Longitude-corrected span so the track isn't stretched
        root._spanX = Math.max((maxLon - minLon) * kx, 1e-9)
        root._spanY = Math.max(maxLat - minLat, 1e-9)
        root._boundsValid = true
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: 16
        visible: root.hasPath

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            if (!root.hasPath)
                return

            if (!root._boundsValid)
                root.recomputeBounds()

            const minLat = root._minLat
            const minLon = root._minLon
            const kx     = root._kx
            const spanX  = root._spanX
            const spanY  = root._spanY

            const pad = 8
            const availW = width - pad * 2
            const availH = height - pad * 2
            const fitScale = Math.min(availW / spanX, availH / spanY)
            const scale = fitScale * root.zoomLevel

            const drawW = spanX * scale
            const drawH = spanY * scale
            const offX = pad + (availW - drawW) / 2.0 + root.panX
            const offY = pad + (availH - drawH) / 2.0 + root.panY

            root._fitDrawW = spanX * fitScale
            root._fitDrawH = spanY * fitScale
            root._availW = availW
            root._availH = availH

            ctx.strokeStyle = "#1E88E5"
            ctx.lineWidth = 3
            ctx.lineJoin = "round"
            ctx.lineCap = "round"
            ctx.beginPath()

            // Project each point inline (no per-point array allocation) — this
            // loop runs on every pan/zoom frame. Y is flipped so north is up.
            for (let i = 0; i < path.length; i += 2) {
                const x = offX + (path[i + 1] - minLon) * kx * scale
                const y = offY + drawH - (path[i] - minLat) * scale
                if (i === 0)
                    ctx.moveTo(x, y)
                else
                    ctx.lineTo(x, y)
            }
            ctx.stroke()

            // Start/finish gate — draw the real A→B segment at its true position
            // and orientation (the gate spans the track width by construction).
            if (root.hasFinish) {
                const ax = offX + (root.finishLon1 - minLon) * kx * scale
                const ay = offY + drawH - (root.finishLat1 - minLat) * scale
                const bx = offX + (root.finishLon2 - minLon) * kx * scale
                const by = offY + drawH - (root.finishLat2 - minLat) * scale
                ctx.strokeStyle = "#FFFFFF"
                ctx.lineWidth = 3
                ctx.lineCap = "butt"
                ctx.beginPath()
                ctx.moveTo(ax, ay)
                ctx.lineTo(bx, by)
                ctx.stroke()
            }
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        WheelHandler {
            target: null
            onWheel: (event) => {
                const factor = event.angleDelta.y > 0 ? 1.25 : (1 / 1.25)
                root.applyZoom(factor, event.x, event.y)
            }
        }

        PinchHandler {
            id: pinchHandler
            target: null
            property real lastScale: 1.0
            onActiveChanged: if (active) lastScale = 1.0
            onScaleChanged: {
                root.applyZoom(scale / lastScale, centroid.position.x, centroid.position.y)
                lastScale = scale
            }
        }

        DragHandler {
            id: dragHandler
            target: null
            enabled: root.zoomLevel > root.minZoom
            property real startPanX: 0
            property real startPanY: 0
            onActiveChanged: {
                if (active) {
                    startPanX = root.panX
                    startPanY = root.panY
                }
            }
            onTranslationChanged: {
                root.panX = startPanX + translation.x
                root.panY = startPanY + translation.y
                root.clampPan()
            }
        }

        TapHandler {
            onDoubleTapped: root.resetView()
        }
    }

    Text {
        anchors.centerIn: parent
        visible: !root.hasPath
        text: "NO TRACK DATA"
        color: "#333333"
        font.pixelSize: 11
        font.bold: true
        font.family: "monospace"
        font.letterSpacing: 2
    }

    onPathChanged: {
        root._boundsValid = false
        root.resetView()
        canvas.requestPaint()
    }
    // Finish-gate endpoints feed the cached bounds too; invalidate on change.
    onFinishLat1Changed: { root._boundsValid = false; canvas.requestPaint() }
    onFinishLon1Changed: { root._boundsValid = false; canvas.requestPaint() }
    onFinishLat2Changed: { root._boundsValid = false; canvas.requestPaint() }
    onFinishLon2Changed: { root._boundsValid = false; canvas.requestPaint() }
}
