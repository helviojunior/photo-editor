import React, { useCallback, useEffect, useRef, useState } from "react";
import { useNavigate } from "react-router-dom";
import { ArrowLeft, CheckCircle2, Info, Instagram, KeyRound, LogOut } from "lucide-react";
import api from "lib/api";
import { useI18n } from "i18n";
import { useDialog } from "contexts/DialogContext";
import { Button } from "components/ui/button";
import { Card, CardContent, CardHeader } from "components/ui/card";
import { Input } from "components/ui/input";
import { Label } from "components/ui/label";
import { FormErrors } from "components/ui/form-error";
import { CAPTION_FIELDS } from "components/instagram/caption";
import { DEFAULT_CAPTION_TEMPLATE } from "components/instagram/InstagramPublishDialog";

/**
 * Configurações > Instagram: a conta para onde o "Publicar no Instagram"
 * envia e o modelo da legenda.
 *
 * O login é o do app do celular (usuário e senha); o backend guarda só a
 * sessão em ~/.photoe, nunca a senha. Ele pode parar no meio pedindo um
 * código — verificação em duas etapas, ou o "confirme que é você" do
 * Instagram —, e então a tela troca o formulário pelo campo do código.
 * A resposta do backend sai quando o login conclui, falha ou pede código; se
 * demorar, a tela consulta de novo até ele andar.
 */
export default function InstagramSettings() {
  const { t, tf } = useI18n();
  const navigate = useNavigate();
  const { confirm } = useDialog();
  const [account, setAccount] = useState(null);
  const [loadError, setLoadError] = useState(false);
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [code, setCode] = useState("");
  const [busy, setBusy] = useState("");
  const [error, setError] = useState("");
  const [template, setTemplate] = useState("");
  const [templateSaved, setTemplateSaved] = useState(false);
  // O erro do login so vale para um login feito nesta visita: o backend
  // guarda o ultimo ate o proximo comecar.
  const [tried, setTried] = useState(false);
  const mounted = useRef(true);
  useEffect(() => () => { mounted.current = false; }, []);

  const apply = useCallback((data) => {
    if (mounted.current) setAccount(data);
  }, []);

  useEffect(() => {
    api.get("/api/instagram/account/")
      .then((res) => {
        apply(res.data);
        setTemplate(res.data.caption_template || t("instagram.captionTemplate", DEFAULT_CAPTION_TEMPLATE));
      })
      .catch(() => setLoadError(true));
  }, [apply, t]);

  const login = account?.login;
  const loginState = login?.state;
  const waitingCode = loginState === "code";

  // Login ainda em andamento quando a resposta voltou: consulta até andar.
  useEffect(() => {
    if (loginState !== "running") return undefined;
    let alive = true;
    (async () => {
      while (alive) {
        try {
          const res = await api.get("/api/instagram/login/");
          if (!alive) return;
          apply(res.data);
          if (res.data?.login?.state !== "running") return;
        } catch {
          return;
        }
      }
    })();
    return () => { alive = false; };
  }, [loginState, apply]);

  // true se a chamada deu certo.
  const run = async (name, fn) => {
    setError("");
    setBusy(name);
    try {
      apply((await fn()).data);
      return true;
    } catch (err) {
      setError(err?.response?.data?.error || t("error.generic"));
      return false;
    } finally {
      if (mounted.current) setBusy("");
    }
  };

  const connect = async (e) => {
    e.preventDefault();
    setTried(true);
    if (await run("login", () => api.post("/api/instagram/login/", { username, password }))) {
      setPassword("");
      setCode("");
    }
  };

  const sendCode = async (e) => {
    e.preventDefault();
    if (await run("code", () => api.post("/api/instagram/login/code/", { code }))) setCode("");
  };

  const cancel = () => run("cancel", () => api.delete("/api/instagram/login/"));

  const disconnect = () => confirm({
    title: t("instagram.settings.disconnectTitle", "Disconnect the account?"),
    description: <>
      {tf("instagram.settings.disconnectText", { username: account?.username || "" })}
    </>,
    variant: "warning",
    confirmLabel: t("instagram.settings.disconnect", "Disconnect"),
    onConfirm: async () => apply((await api.delete("/api/instagram/account/")).data),
  });

  const saveTemplate = async (e) => {
    e.preventDefault();
    setTemplateSaved(false);
    if (await run("template", () => api.put("/api/instagram/account/", { caption_template: template }))) {
      setTemplateSaved(true);
    }
  };

  const pending = busy === "login" || busy === "code" || loginState === "running";

  return (
    <div className="w-full space-y-4">
      <div className="flex flex-wrap items-center gap-2">
        <Button variant="ghost" size="sm" onClick={() => navigate(-1)}>
          <ArrowLeft className="h-4 w-4" /> {t("common.back")}
        </Button>
        <h1 className="flex items-center gap-2 text-lg font-semibold">
          <Instagram className="h-5 w-5 text-brand-400" aria-hidden="true" />
          {t("instagram.settings.title", "Instagram")}
        </h1>
      </div>

      {loadError && <FormErrors items={[t("common.loadError")]} />}

      <Card className="w-full">
        <CardHeader>
          <h2 className="font-semibold">{t("instagram.settings.account", "Account")}</h2>
          <p className="text-sm text-muted-foreground">
            {t("instagram.settings.accountHint",
              "The account the photos are published to. The app keeps only the Instagram session on this computer — never the password.")}
          </p>
        </CardHeader>
        <CardContent className="space-y-4">
          {account?.connected && !waitingCode && (
            <div className="flex flex-wrap items-center justify-between gap-3 rounded-md border border-border px-3 py-2">
              <p className="flex items-center gap-2 text-sm">
                <CheckCircle2 className="h-4 w-4 text-emerald-600 dark:text-emerald-400" aria-hidden="true" />
                {tf("instagram.settings.connected", { username: account.username })}
              </p>
              <Button variant="outline" size="sm" onClick={disconnect}>
                <LogOut className="h-4 w-4" /> {t("instagram.settings.disconnect", "Disconnect")}
              </Button>
            </div>
          )}

          {waitingCode ? (
            <form onSubmit={sendCode} className="space-y-3">
              <p className="flex items-start gap-2 text-sm">
                <KeyRound className="mt-0.5 h-4 w-4 flex-shrink-0 text-brand-400" aria-hidden="true" />
                {login.kind === "two_factor"
                  ? t("instagram.settings.codeTwoFactor",
                    "Enter the code from your authenticator app or the SMS Instagram sent.")
                  : t("instagram.settings.codeChallenge",
                    "Instagram sent a security code by e-mail or SMS to confirm this login. Enter it below.")}
              </p>
              <div className="grid gap-4 md:grid-cols-2">
                <div className="space-y-1.5">
                  <Label htmlFor="instagram-code">{t("instagram.settings.code", "Code")}</Label>
                  <Input id="instagram-code" value={code} autoFocus
                    inputMode="numeric" autoComplete="one-time-code"
                    onChange={(e) => setCode(e.target.value)} />
                </div>
              </div>
              <div className="flex flex-wrap gap-2">
                <Button type="submit" loading={pending} disabled={!code.trim()}>
                  {t("common.confirm")}
                </Button>
                <Button type="button" variant="ghost" onClick={cancel} disabled={busy === "cancel"}>
                  {t("common.cancel")}
                </Button>
              </div>
            </form>
          ) : (
            <form onSubmit={connect} className="space-y-3">
              {account?.connected && (
                <p className="text-sm text-muted-foreground">
                  {t("instagram.settings.switch", "To use another account, connect it below.")}
                </p>
              )}
              <div className="grid gap-4 md:grid-cols-2">
                <div className="space-y-1.5">
                  <Label htmlFor="instagram-username">{t("instagram.settings.username", "Username")}</Label>
                  <Input id="instagram-username" value={username} autoComplete="username"
                    autoCapitalize="none" spellCheck={false} placeholder="@"
                    onChange={(e) => setUsername(e.target.value)} />
                </div>
                <div className="space-y-1.5">
                  <Label htmlFor="instagram-password">{t("instagram.settings.password", "Password")}</Label>
                  <Input id="instagram-password" type="password" value={password}
                    autoComplete="current-password"
                    onChange={(e) => setPassword(e.target.value)} />
                </div>
              </div>
              <Button type="submit" loading={pending} disabled={!username.trim() || !password}>
                {!pending && <Instagram className="h-4 w-4" />}
                {pending ? t("instagram.settings.connecting", "Connecting…")
                  : t("instagram.settings.connect", "Connect")}
              </Button>
            </form>
          )}

          <FormErrors items={[
            error,
            tried && loginState === "error" && (login.message || t("error.generic")),
            tried && loginState === "error" && login.detail,
          ]} />

          <p className="flex items-start gap-2 rounded-md border border-border bg-muted/40 px-3 py-2 text-xs text-muted-foreground">
            <Info className="mt-px h-3.5 w-3.5 flex-shrink-0" aria-hidden="true" />
            {t("instagram.settings.warning",
              "The app signs in like the Instagram phone app, outside Meta's official API. Instagram may ask you to confirm the login or temporarily limit the account; publishing at a normal pace avoids that.")}
          </p>
        </CardContent>
      </Card>

      <Card className="w-full">
        <CardHeader>
          <h2 className="font-semibold">{t("instagram.settings.captionTitle", "Caption template")}</h2>
          <p className="text-sm text-muted-foreground">
            {t("instagram.settings.captionHint",
              "Fills the caption of each post with the event data. You can still edit it before publishing.")}
          </p>
        </CardHeader>
        <CardContent>
          <form onSubmit={saveTemplate} className="space-y-3">
            <textarea value={template} rows={6} aria-label={t("instagram.settings.captionTitle", "Caption template")}
              onChange={(e) => { setTemplate(e.target.value); setTemplateSaved(false); }}
              className="w-full resize-y rounded-md border border-input bg-background px-3 py-2 text-sm text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring" />
            <ul className="grid gap-1 text-xs text-muted-foreground md:grid-cols-2">
              {CAPTION_FIELDS.map((f) => (
                <li key={f}>
                  <code className="rounded bg-muted px-1 text-foreground">{`{${f}}`}</code>{" "}
                  {t(`instagram.field.${f}`, f)}
                </li>
              ))}
            </ul>
            <div className="flex flex-wrap items-center gap-3">
              <Button type="submit" loading={busy === "template"}>{t("common.save")}</Button>
              <Button type="button" variant="ghost"
                onClick={() => { setTemplate(t("instagram.captionTemplate", DEFAULT_CAPTION_TEMPLATE)); setTemplateSaved(false); }}>
                {t("instagram.settings.captionDefault", "Use the default")}
              </Button>
              {templateSaved && (
                <span className="inline-flex items-center gap-1 text-xs text-emerald-600 dark:text-emerald-400" role="status">
                  <CheckCircle2 className="h-3.5 w-3.5" aria-hidden="true" /> {t("instagram.settings.saved", "Saved")}
                </span>
              )}
            </div>
          </form>
        </CardContent>
      </Card>
    </div>
  );
}
