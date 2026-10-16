import QtQuick
import PhotoEditor

// Miniatura de um efeito na biblioteca: a mesma cena pequena (céu, sol,
// morro, chão — cores do Theme) redesenhada como o efeito a deixaria. É uma
// ilustração, não o motor: dá a ideia do efeito sem gerar uma imagem.
Canvas {
    id: swatch

    property string effect: ""

    implicitHeight: 56
    onEffectChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function clamp(v) {
        return Math.min(1, Math.max(0, v));
    }
    /// A cor da cena depois do efeito (só os que mudam cor).
    function tone(c) {
        let r = c.r, g = c.g, b = c.b;
        const y = 0.3 * r + 0.59 * g + 0.11 * b;
        switch (effect) {
        case "bw":
            r = g = b = y;
            break;
        case "sepia":
            r = y * 1.07 + 0.08; g = y * 0.87 + 0.03; b = y * 0.62;
            break;
        case "vintage":
            // Quente, desbotado e com menos cor.
            r = 0.7 * (0.6 * r + 0.4 * y) + 0.26; g = 0.62 * (0.6 * g + 0.4 * y) + 0.18; b = 0.36 * (0.6 * b + 0.4 * y) + 0.14;
            break;
        case "fade":
            r = 0.5 * r + 0.3; g = 0.5 * g + 0.3; b = 0.5 * b + 0.32;
            break;
        case "matte":
            // Pretos levantados, contraste baixo, cor contida.
            r = 0.24 + 0.68 * (0.6 * r + 0.4 * y);
            g = 0.24 + 0.68 * (0.6 * g + 0.4 * y);
            b = 0.26 + 0.68 * (0.6 * b + 0.4 * y);
            break;
        }
        return "rgba(" + Math.round(clamp(r) * 255) + ", " + Math.round(clamp(g) * 255) + ", "
            + Math.round(clamp(b) * 255) + ", 1)";
    }

    onPaint: {
        const ctx = getContext("2d");
        ctx.reset();
        const w = width, h = height;
        const horizon = h * 0.62;
        // Céu
        const sky = ctx.createLinearGradient(0, 0, 0, horizon);
        sky.addColorStop(0, tone(Theme.swatchSkyTop));
        sky.addColorStop(1, tone(Theme.swatchSkyBottom));
        ctx.fillStyle = sky;
        ctx.fillRect(0, 0, w, horizon + 1);
        // Sol
        ctx.fillStyle = tone(Theme.swatchSun);
        ctx.beginPath();
        ctx.arc(w * 0.7, horizon * 0.55, h * 0.13, 0, Math.PI * 2, false);
        ctx.fill();
        // Morro
        ctx.fillStyle = tone(Theme.swatchHill);
        ctx.beginPath();
        ctx.moveTo(0, horizon + 2);
        ctx.quadraticCurveTo(w * 0.3, h * 0.28, w * 0.62, horizon + 2);
        ctx.closePath();
        ctx.fill();
        // Chão
        ctx.fillStyle = tone(Theme.swatchGround);
        ctx.fillRect(0, horizon, w, h - horizon);

        if (effect === "vignette") {
            const r = Math.hypot(w, h) / 2;
            const v = ctx.createRadialGradient(w / 2, h / 2, r * 0.35, w / 2, h / 2, r);
            v.addColorStop(0, Theme.css(Theme.black, 0));
            v.addColorStop(1, Theme.css(Theme.black, 0.85));
            ctx.fillStyle = v;
            ctx.fillRect(0, 0, w, h);
        } else if (effect === "grain") {
            // Grão determinístico (a miniatura não "ferve" a cada repintura).
            let seed = 7;
            const next = () => {
                seed = (seed * 1103515245 + 12345) % 2147483648;
                return seed / 2147483648;
            };
            for (let i = 0; i < w * h / 6; ++i) {
                const light = next() > 0.5;
                ctx.fillStyle = light ? Theme.css(Theme.white, 0.22) : Theme.css(Theme.black, 0.28);
                ctx.fillRect(Math.floor(next() * w), Math.floor(next() * h), 1, 1);
            }
        }
    }
}
