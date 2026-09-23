// Ordens da filmstrip. A navegação (← →), o contador e a vizinha aberta após
// excluir seguem a ordem escolhida, porque todos leem a mesma lista ordenada.
export const SORT_OPTIONS = ["date_asc", "date_desc", "name_asc", "name_desc"];
export const DEFAULT_SORT = "date_asc";

const STORAGE_KEY = "photoeditor.sort";

// Nome com números em ordem natural: IMG_9 antes de IMG_10.
const byName = (a, b) =>
  a.file_name.localeCompare(b.file_name, undefined, { numeric: true, sensitivity: "base" });

// Sem data de captura vai para o fim nas duas direções — não há onde encaixá-la.
function byDate(a, b, dir) {
  if (!a.captured_at || !b.captured_at) {
    if (a.captured_at) return -1;
    if (b.captured_at) return 1;
    return byName(a, b);
  }
  const d = new Date(a.captured_at) - new Date(b.captured_at);
  return d ? d * dir : byName(a, b) * dir;
}

export function sortPhotos(photos, sort) {
  if (!photos) return photos;
  const list = [...photos];
  switch (sort) {
    case "date_desc": return list.sort((a, b) => byDate(a, b, -1));
    case "name_asc": return list.sort(byName);
    case "name_desc": return list.sort((a, b) => byName(b, a));
    default: return list.sort((a, b) => byDate(a, b, 1));
  }
}

// Preferência de quem está vendo, só no navegador: storage pode estar
// bloqueado (aba privada), e aí vale o padrão.
export function readSort() {
  try {
    const v = localStorage.getItem(STORAGE_KEY);
    return SORT_OPTIONS.includes(v) ? v : DEFAULT_SORT;
  } catch {
    return DEFAULT_SORT;
  }
}

export function writeSort(value) {
  try { localStorage.setItem(STORAGE_KEY, value); } catch { /* sem storage */ }
}
