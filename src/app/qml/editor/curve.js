.pragma library

// A curva do painel Curvas, desenhada com a MESMA conta do motor: cúbica de
// Hermite com as tangentes de Fritsch–Carlson (porte linha a linha do
// ``Spline`` de src/imaging/Look.cpp). Assim o que se vê no gráfico é o que
// a foto recebe — passa pelos pontos, não oscila e é monótona onde os
// pontos são. Antes dos extremos e depois deles, constante (o y do extremo).
//
//   import "curve.js" as Curve
//   const f = Curve.spline([[0, 0], [0.25, 0.2], [0.75, 0.8], [1, 1]]);
//   f(0.5)              // y em [0, 1]
//   Curve.sample(points, 256)   // [[x, y], ...] para desenhar
//
// JS puro (nada de Qt): roda no node para conferir contra o C++.

/// Pontos [[x, y], ...] (ou [{x, y}]) ordenados por x; menos de 2 = identidade.
function normalize(points) {
    const out = [];
    // Vindos do C++ (QVariantList), os pares não são Array de verdade
    // (Array.isArray dá false): o teste é pelo formato {x, y}.
    const list = points || [];
    for (let i = 0; i < list.length; ++i) {
        const p = list[i];
        if (p === null || p === undefined)
            continue;
        const pair = p.x === undefined;
        const x = Number(pair ? p[0] : p.x);
        const y = Number(pair ? p[1] : p.y);
        if (isFinite(x) && isFinite(y))
            out.push([Math.min(1, Math.max(0, x)), Math.min(1, Math.max(0, y))]);
    }
    out.sort((a, b) => a[0] - b[0]);
    return out.length < 2 ? [[0, 0], [1, 1]] : out;
}

/// A função y = f(x) da curva.
function spline(points) {
    const p = normalize(points);
    const n = p.length;
    const d = [];
    for (let k = 0; k + 1 < n; ++k)
        d.push((p[k + 1][1] - p[k][1]) / (p[k + 1][0] - p[k][0]));
    const m = new Array(n).fill(0);
    m[0] = d[0];
    m[n - 1] = d[n - 2];
    for (let k = 1; k + 1 < n; ++k)
        m[k] = (d[k - 1] * d[k] <= 0) ? 0 : (d[k - 1] + d[k]) / 2;
    for (let k = 0; k + 1 < n; ++k) {
        if (d[k] === 0) {
            m[k] = 0;
            m[k + 1] = 0;
            continue;
        }
        const a = m[k] / d[k];
        const b = m[k + 1] / d[k];
        const s = a * a + b * b;
        if (s > 9) {
            const t = 3 / Math.sqrt(s);
            m[k] = t * a * d[k];
            m[k + 1] = t * b * d[k];
        }
    }
    return function (x) {
        if (x <= p[0][0])
            return p[0][1];
        if (x >= p[n - 1][0])
            return p[n - 1][1];
        let k = 0;
        while (k + 2 < n && x >= p[k + 1][0])
            ++k;
        const h = p[k + 1][0] - p[k][0];
        const t = (x - p[k][0]) / h;
        const t2 = t * t, t3 = t2 * t;
        const y = (2 * t3 - 3 * t2 + 1) * p[k][1] + (t3 - 2 * t2 + t) * h * m[k]
            + (-2 * t3 + 3 * t2) * p[k + 1][1] + (t3 - t2) * h * m[k + 1];
        return Math.min(1, Math.max(0, y));
    };
}

/// ``count`` + 1 pontos [[x, y]] igualmente espaçados em x, de 0 a 1.
function sample(points, count) {
    const f = spline(points);
    const out = [];
    for (let i = 0; i <= count; ++i)
        out.push([i / count, f(i / count)]);
    return out;
}

/// A curva é a identidade (a reta de [0, 0] a [1, 1])?
function isIdentity(points) {
    const p = normalize(points);
    return p.length === 2 && p[0][0] === 0 && p[0][1] === 0 && p[1][0] === 1 && p[1][1] === 1;
}
