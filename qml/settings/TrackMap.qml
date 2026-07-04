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

    // Projects a lat/lon to canvas pixels. Declared once at the component
    // level (rather than inside onPaint) so repainting doesn't allocate a
    // fresh closure per call.
    function toPoint(lat, lon, minLon, minLat, kx, scale, offX, offY, drawH) {
        const x = offX + (lon - minLon) * kx * scale
        // Flip Y so north is up
        const y = offY + drawH - (lat - minLat) * scale
        return [x, y]
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

            let minLat = path[0], maxLat = path[0]
            let minLon = path[1], maxLon = path[1]
            for (let i = 0; i < path.length; i += 2) {
                const lat = path[i], lon = path[i + 1]
                if (lat < minLat) minLat = lat
                if (lat > maxLat) maxLat = lat
                if (lon < minLon) minLon = lon
                if (lon > maxLon) maxLon = lon
            }
            // Include the finish gate so it's never clipped outside the map
            if (root.hasFinish) {
                const glat = [root.finishLat1, root.finishLat2]
                const glon = [root.finishLon1, root.finishLon2]
                for (let k = 0; k < 2; k++) {
                    if (glat[k] < minLat) minLat = glat[k]
                    if (glat[k] > maxLat) maxLat = glat[k]
                    if (glon[k] < minLon) minLon = glon[k]
                    if (glon[k] > maxLon) maxLon = glon[k]
                }
            }

            const midLat = (minLat + maxLat) / 2.0
            const kx = Math.cos(midLat * Math.PI / 180.0)

            // Longitude-corrected span so the track isn't stretched
            const spanX = Math.max((maxLon - minLon) * kx, 1e-9)
            const spanY = Math.max(maxLat - minLat, 1e-9)

            const pad = 8
            const availW = width - pad * 2
            const availH = height - pad * 2
            const scale = Math.min(availW / spanX, availH / spanY)

            const drawW = spanX * scale
            const drawH = spanY * scale
            const offX = pad + (availW - drawW) / 2.0
            const offY = pad + (availH - drawH) / 2.0

            ctx.strokeStyle = "#1E88E5"
            ctx.lineWidth = 3
            ctx.lineJoin = "round"
            ctx.lineCap = "round"
            ctx.beginPath()

            for (let i = 0; i < path.length; i += 2) {
                const p = root.toPoint(path[i], path[i + 1], minLon, minLat, kx, scale, offX, offY, drawH)
                if (i === 0)
                    ctx.moveTo(p[0], p[1])
                else
                    ctx.lineTo(p[0], p[1])
            }
            ctx.stroke()

            // Start/finish gate — draw the real A→B segment at its true position
            // and orientation (the gate spans the track width by construction).
            if (root.hasFinish) {
                const a = root.toPoint(root.finishLat1, root.finishLon1, minLon, minLat, kx, scale, offX, offY, drawH)
                const b = root.toPoint(root.finishLat2, root.finishLon2, minLon, minLat, kx, scale, offX, offY, drawH)
                ctx.strokeStyle = "#FFFFFF"
                ctx.lineWidth = 3
                ctx.lineCap = "butt"
                ctx.beginPath()
                ctx.moveTo(a[0], a[1])
                ctx.lineTo(b[0], b[1])
                ctx.stroke()
            }
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
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

    onPathChanged: canvas.requestPaint()
}
