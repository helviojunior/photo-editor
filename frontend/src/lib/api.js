import axios from "axios";

// O sistema e publico e nao autenticado: nenhuma credencial vai nas chamadas.
const api = axios.create({
  headers: {
    "Content-Type": "application/json",
  },
});

export default api;
