const fs = require("fs");
const path = require("path");

// ---------------------------------------------------------------------------
// Cache-busting build stamp
//
// Um timestamp por build/compilacao, exposto ao app via REACT_APP_BUILD_TS
// (em JS: process.env.REACT_APP_BUILD_TS; no HTML: %REACT_APP_BUILD_TS%).
// Todo objeto estatico recebe o sufixo `?ts=<TS>`, de modo que cada novo build
// invalida o cache do browser/CDN — inclusive os assets externos hospedados no
// media.sec4us.com.br.
//
// Definido aqui (no load do config) para que o valor seja fixado uma unica vez
// antes do react-scripts ler o ambiente, e seja identico em `start` e `build`.
// Um pipeline de CI pode sobrescrever exportando REACT_APP_BUILD_TS.
// ---------------------------------------------------------------------------
process.env.REACT_APP_BUILD_TS =
  process.env.REACT_APP_BUILD_TS || String(Math.floor(Date.now() / 1000));

const BUILD_TS = process.env.REACT_APP_BUILD_TS;

// O CRA so interpola variaveis de ambiente no index.html — os demais arquivos
// de public/ sao copiados verbatim. Este plugin reescreve %REACT_APP_BUILD_TS%
// nos estaticos emitidos (manifest.json e afins) para que as URLs deles tambem
// saiam carimbadas.
const INTERPOLATED_FILES = ["manifest.json", "asset-manifest.json"];

class InterpolateStaticPlugin {
  apply(compiler) {
    compiler.hooks.afterEmit.tap("InterpolateStaticPlugin", () => {
      const outDir = compiler.options.output.path || "";
      for (const file of INTERPOLATED_FILES) {
        const outFile = path.join(outDir, file);
        try {
          const src = fs.readFileSync(outFile, "utf8");
          if (src.includes("%REACT_APP_BUILD_TS%")) {
            fs.writeFileSync(outFile, src.split("%REACT_APP_BUILD_TS%").join(BUILD_TS));
          }
        } catch (e) {
          // Arquivo inexistente (ou dev server servindo public/ da memoria) — ignora.
        }
      }
    });
  }
}

// ---------------------------------------------------------------------------
// Imports absolutos (`components/...`, `lib/...`, `i18n`)
//
// Antes saiam de graca do `baseUrl: "src"` no jsconfig.json: o react-scripts
// lia aquele campo e configurava webpack e jest sozinho. O `baseUrl` esta
// deprecado e para de funcionar no TypeScript 7.0, entao o jsconfig passou a
// declarar `paths` — que o EDITOR entende, mas que o react-scripts nao le.
//
// Por isso a resolucao do BUILD e dos TESTES e' declarada aqui. As raizes
// abaixo tem de concordar com o `paths` do jsconfig.json: divergir faz o
// editor achar um arquivo que o build nao acha (ou o contrario), e o erro so
// aparece na compilacao.
//
// O jsconfig.json nao tem comentario explicando isso porque NAO PODE ter: o
// react-scripts o carrega com require(), que e' JSON estrito e rebenta com
// `//`. A explicacao mora aqui.
// ---------------------------------------------------------------------------
const SRC = path.resolve(__dirname, 'src');
const RAIZES = ['components', 'contexts', 'i18n', 'lib', 'pages'];
const ALIAS = Object.fromEntries(RAIZES.map((r) => [r, path.join(SRC, r)]));

module.exports = {
  style: {
    postcss: {
      mode: 'extends',
      loaderOptions: (postcssLoaderOptions) => {
        return postcssLoaderOptions;
      },
    },
  },
  webpack: {
    alias: ALIAS,
    plugins: {
      add: [new InterpolateStaticPlugin()],
    },
  },
  jest: {
    configure: (config) => ({
      ...config,
      // `modulePaths` faz o jest tratar `src/` como raiz de resolucao, que e'
      // exatamente o que o `baseUrl` fazia por ele.
      modulePaths: [SRC],
    }),
  },
};
