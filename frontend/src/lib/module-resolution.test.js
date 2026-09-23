/**
 * O editor e o build têm de resolver os mesmos imports absolutos.
 *
 * São duas declarações separadas, por um motivo que não dá para contornar:
 *
 *  - `jsconfig.json` → `paths`, que é o que o EDITOR lê (e substituiu o
 *    `baseUrl`, deprecado e removido no TypeScript 7.0);
 *  - `craco.config.js` → `alias` do webpack e `modulePaths` do jest, que é o
 *    que o BUILD e os TESTES leem. O react-scripts não entende `paths`.
 *
 * Divergir é um defeito silencioso do pior tipo: o editor continua achando o
 * arquivo, o autocompletar funciona, e a falha só aparece na compilação — ou,
 * pior, só no teste que ninguém rodou. Este teste compara as duas listas.
 */
const fs = require("fs");
const path = require("path");

const RAIZ = path.resolve(__dirname, "../..");

describe("imports absolutos", () => {
  const jsconfig = JSON.parse(fs.readFileSync(path.join(RAIZ, "jsconfig.json"), "utf8"));
  const craco = fs.readFileSync(path.join(RAIZ, "craco.config.js"), "utf8");

  const raizesDoJsconfig = [
    ...new Set(Object.keys(jsconfig.compilerOptions.paths).map((k) => k.split("/")[0])),
  ].sort();

  // Aspas simples ou duplas: o estilo varia entre projetos, e um teste que só
  // aceita um dos dois falha por formatação, não por divergência real.
  const raizesDoCraco = [
    ...(craco.match(/const RAIZES = \[([^\]]*)\]/)[1].matchAll(/["']([^"']+)["']/g)),
  ].map((m) => m[1]).sort();

  test("jsconfig e craco declaram as MESMAS raízes", () => {
    expect(raizesDoCraco).toEqual(raizesDoJsconfig);
  });

  test("toda raiz declarada existe em src/", () => {
    raizesDoJsconfig.forEach((r) => {
      expect(fs.existsSync(path.join(RAIZ, "src", r))).toBe(true);
    });
  });

  test("toda raiz usada no código está declarada", () => {
    // Varre os imports reais: uma pasta nova em src/ que alguém importe sem
    // declarar funciona no editor (por acaso) e quebra no build.
    const usadas = new Set();
    const anda = (dir) => {
      for (const nome of fs.readdirSync(dir)) {
        const alvo = path.join(dir, nome);
        if (fs.statSync(alvo).isDirectory()) { anda(alvo); continue; }
        if (!/\.jsx?$/.test(nome)) continue;
        const src = fs.readFileSync(alvo, "utf8");
        for (const m of src.matchAll(/from\s+"([^".][^"]*)"/g)) {
          const raiz = m[1].split("/")[0];
          if (fs.existsSync(path.join(RAIZ, "src", raiz))) usadas.add(raiz);
        }
      }
    };
    anda(path.join(RAIZ, "src"));
    [...usadas].forEach((r) => expect(raizesDoJsconfig).toContain(r));
  });

  test("o baseUrl deprecado não voltou", () => {
    // Se voltar, o editor passa a resolver por ele e a divergência com o craco
    // deixa de doer — até o TypeScript 7.0 removê-lo.
    expect(jsconfig.compilerOptions.baseUrl).toBeUndefined();
  });
});
