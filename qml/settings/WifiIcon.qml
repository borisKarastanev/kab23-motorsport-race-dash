import QtQuick 2.15

// Hand-drawn wifi glyph (dot + two fanning arcs) — used in place of a wifi
// emoji so the icon is tintable and has no color-glyph background chip.
Item {
    id: root
    property color color: "#448844"
    property int size: 14

    implicitWidth: size
    implicitHeight: size

    Canvas {
        id: canvas
        anchors.fill: parent

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = root.color
            ctx.fillStyle = root.color
            ctx.lineWidth = Math.max(1, root.size / 8)
            ctx.lineCap = "round"

            const cx = width / 2
            const cy = height * 0.85
            const dotR = root.size * 0.09

            ctx.beginPath()
            ctx.arc(cx, cy, dotR, 0, Math.PI * 2)
            ctx.fill()

            for (let i = 1; i <= 2; i++) {
                const r = root.size * 0.28 * i
                ctx.beginPath()
                ctx.arc(cx, cy, r, Math.PI * 1.2, Math.PI * 1.8)
                ctx.stroke()
            }
        }
    }

    onColorChanged: canvas.requestPaint()
    onSizeChanged: canvas.requestPaint()
    Component.onCompleted: canvas.requestPaint()
}
