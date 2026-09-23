import React, { useEffect, useState } from "react";
import { BrowserRouter, Routes, Route, Navigate } from "react-router-dom";
import { DialogProvider } from "contexts/DialogContext";
import { I18nProvider } from "i18n";
import AppLayout from "components/layout/AppLayout";
import Editor from "pages/Editor";
import "./App.css";

function App() {
  const [darkMode, setDarkMode] = useState(true);

  useEffect(() => {
    if (darkMode) {
      document.documentElement.classList.add("dark");
    } else {
      document.documentElement.classList.remove("dark");
    }
  }, [darkMode]);

  return (
    <div className="App">
      <BrowserRouter>
        {/* Sistema publico e nao autenticado: nao ha login nem rota protegida. */}
        <I18nProvider>
          <DialogProvider>
            <Routes>
              <Route element={<AppLayout darkMode={darkMode} setDarkMode={setDarkMode} />}>
                {/* Uma rota por foto: navegar atualiza a URL (regra 3). */}
                <Route path="/photos" element={<Editor />} />
                <Route path="/photos/:id" element={<Editor />} />
              </Route>

              <Route path="/" element={<Navigate to="/photos" replace />} />
              <Route path="*" element={<Navigate to="/photos" replace />} />
            </Routes>
          </DialogProvider>
        </I18nProvider>
      </BrowserRouter>
    </div>
  );
}

export default App;
