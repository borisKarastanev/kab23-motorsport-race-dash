import QtQuick 2.15

Item {
    id: root

    // Flat [lat, lon, lat, lon, …] list for the lap being displayed
    property var path: []

    // Exact coordinates set via the "Set Finish Line" button (dashConfig.finishLineLat/Lon)
    property real finishLat: 0
    property real finishLon: 0

    readonly property bool hasPath: path.length >= 4
    readonly property bool hasFinish: finishLat !== 0 || finishLon !== 0

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
            // Include the finish line so its marker is never clipped outside the map
            if (root.hasFinish) {
                if (root.finishLat < minLat) minLat = root.finishLat
                if (root.finishLat > maxLat) maxLat = root.finishLat
                if (root.finishLon < minLon) minLon = root.finishLon
                if (root.finishLon > maxLon) maxLon = root.finishLon
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

            // Start/finish marker — a dash perpendicular to the direction of travel,
            // drawn at the exact coordinates set via the "Set Finish Line" button.
            // Orientation comes from the first two path samples (the local track
            // tangent), independent of the marker's own position, so the dash is
            // always a clean 90° cross regardless of how far the marker sits from
            // the first captured GPS fix.
            if (root.hasFinish) {
                const start   = root.toPoint(root.finishLat, root.finishLon, minLon, minLat, kx, scale, offX, offY, drawH)
                const dirFrom = root.toPoint(path[0], path[1], minLon, minLat, kx, scale, offX, offY, drawH)
                const dirTo   = root.toPoint(path[2], path[3], minLon, minLat, kx, scale, offX, offY, drawH)

                let dx = dirTo[0] - dirFrom[0]
                let dy = dirTo[1] - dirFrom[1]
                const len = Math.hypot(dx, dy)
                if (len > 1e-6) {
                    dx /= len
                    dy /= len
                } else {
                    dx = 1
                    dy = 0
                }
                // Perpendicular to travel direction
                const px = -dy
                const py = dx

                const halfLen = 10
                ctx.strokeStyle = "#FFFFFF"
                ctx.lineWidth = 3
                ctx.lineCap = "butt"
                ctx.beginPath()
                ctx.moveTo(start[0] - px * halfLen, start[1] - py * halfLen)
                ctx.lineTo(start[0] + px * halfLen, start[1] + py * halfLen)
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
