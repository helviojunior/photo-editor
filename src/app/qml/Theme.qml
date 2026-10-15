pragma Singleton
import QtQuick

// Tokens visuais do app — o equivalente QML do index.css (variáveis do tema
// claro/escuro) e do tailwind.config.js (escala brand-*). Toda cor, raio,
// fonte e espaçamento sai daqui: os componentes só referenciam o Theme, nunca
// um "#rrggbb" solto.
//
// Regra de cor (CLAUDE.md 2.1): vermelho da marca = acento; esmeralda =
// confirmação; erro = FormError (caixa + ícone), nunca só texto colorido.
QtObject {
    id: theme

    // Tema escuro é o padrão (App.darkTheme, persistido pelo C++).
    readonly property bool dark: App.darkTheme

    // ------------------------------------------------------------------ //
    // Paleta fixa (Tailwind) — independe do tema
    // ------------------------------------------------------------------ //

    // Vermelho da marca, derivado do #ef3236 do logotipo (mesma matiz,
    // luminosidade variando). Num fork, troque a escala inteira.
    readonly property color brand50: "#fcf0f0"
    readonly property color brand100: "#fbdedf"
    readonly property color brand200: "#fac0c1"
    readonly property color brand300: "#f99698"
    readonly property color brand400: "#f85e62"
    readonly property color brand500: "#ef3236"
    readonly property color brand600: "#e21418"
    readonly property color brand700: "#b91518"
    readonly property color brand800: "#921619"
    readonly property color brand900: "#791719"

    readonly property color emerald100: "#d1fae5"
    readonly property color emerald400: "#34d399"
    readonly property color emerald600: "#059669"
    readonly property color emerald900: "#064e3b"

    readonly property color amber100: "#fef3c7"
    readonly property color amber400: "#fbbf24"
    readonly property color amber600: "#d97706"
    readonly property color amber900: "#78350f"

    readonly property color red100: "#fee2e2"
    readonly property color red400: "#f87171"
    readonly property color red500: "#ef4444"
    readonly property color red600: "#dc2626"
    readonly property color red900: "#7f1d1d"

    readonly property color blue100: "#dbeafe"
    readonly property color blue400: "#60a5fa"
    readonly property color blue600: "#2563eb"
    readonly property color blue900: "#1e3a8a"

    readonly property color indigo600: "#4f46e5"
    readonly property color indigo700: "#4338ca"

    readonly property color neutral800: "#262626"
    readonly property color neutral900: "#171717"
    readonly property color neutral950: "#0a0a0a"
    readonly property color zinc950: "#09090b"

    readonly property color white: "#ffffff"
    readonly property color black: "#000000"

    // ------------------------------------------------------------------ //
    // Tokens do tema (index.css :root / .dark)
    // ------------------------------------------------------------------ //

    readonly property color background: dark ? "#171717" : "#ffffff"
    readonly property color foreground: dark ? "#f2f2f2" : "#0a0a0a"
    readonly property color card: dark ? "#1f1f1f" : "#ffffff"
    readonly property color cardForeground: foreground
    readonly property color popover: card
    readonly property color popoverForeground: foreground
    // No escuro o "primary" é o azul do botão Exportar/Concluir das telas.
    readonly property color primary: dark ? "#3c83f6" : "#171717"
    readonly property color primaryForeground: "#fafafa"
    readonly property color primaryHover: indigo600
    readonly property color primaryPressed: indigo700
    readonly property color secondary: dark ? "#292929" : "#f5f5f5"
    readonly property color secondaryForeground: dark ? "#f2f2f2" : "#171717"
    readonly property color muted: dark ? "#292929" : "#f5f5f5"
    readonly property color mutedForeground: dark ? "#999999" : "#737373"
    readonly property color accent: dark ? "#292929" : "#f5f5f5"
    readonly property color accentForeground: dark ? "#f2f2f2" : "#171717"
    readonly property color destructive: dark ? "#811d1d" : "#ef4444"
    readonly property color destructiveForeground: "#fafafa"
    readonly property color destructiveHover: red600
    readonly property color border: dark ? "#333333" : "#e5e5e5"
    readonly property color input: border
    readonly property color ring: dark ? "#3c83f6" : "#0a0a0a"
    // Anel de foco das telas (focus-visible:ring-brand-400).
    readonly property color focusRing: brand400

    // Fundo das áreas de foto (antes/depois) e da filmstrip.
    readonly property color imagePane: neutral900
    readonly property color filmstrip: neutral900
    readonly property color imageLabel: Qt.rgba(0, 0, 0, 0.6)

    // Modal: fundo escurecido (bg-black/40).
    readonly property color backdrop: Qt.rgba(0, 0, 0, 0.4)
    readonly property color overlay: Qt.rgba(0, 0, 0, 0.6)

    // Splitter (components/ui/splitter.jsx): linhas na cor da borda; ativo
    // = acento a 60% e a pegada num branco suave.
    readonly property color splitterLine: border
    readonly property color splitterBand: background
    readonly property color splitterActive: alpha(brand400, 0.6)
    readonly property color splitterGrip: border
    readonly property color splitterGripActive: alpha(white, 0.7)

    // Barra de rolagem fina (App.css .scrollbar-thin).
    readonly property color scrollbarTrack: "transparent"
    readonly property color scrollbarThumb: alpha(mutedForeground, 0.3)
    readonly property color scrollbarThumbHover: alpha(mutedForeground, 0.5)
    readonly property int scrollbarSize: 8

    // Sucesso/erro/aviso/informação (DialogContext.jsx + form-error.jsx).
    readonly property color success: dark ? emerald400 : emerald600
    readonly property color warning: dark ? amber400 : amber600
    readonly property color info: dark ? blue400 : blue600
    readonly property color danger: dark ? red400 : red600
    readonly property color errorText: red400
    readonly property color errorBorder: alpha(red500, 0.4)
    readonly property color errorBackground: alpha(red500, 0.1)

    // Slider: trilho claro e preenchimento no acento (accent-brand-500).
    readonly property color sliderTrack: dark ? "#e5e5e5" : "#d4d4d4"
    readonly property color sliderFill: brand500
    readonly property color sliderHandle: dark ? "#f5f5f5" : "#ffffff"
    readonly property color sliderHandleBorder: dark ? "#737373" : "#a3a3a3"

    // ------------------------------------------------------------------ //
    // Raio, fonte, espaçamento
    // ------------------------------------------------------------------ //

    readonly property int radiusLg: 8      // rounded-lg (--radius)
    readonly property int radiusMd: 6      // rounded-md
    readonly property int radiusSm: 4      // rounded-sm
    readonly property int radiusXl: 12     // rounded-xl (cards da Home)
    readonly property int radius2xl: 16    // rounded-2xl (modal, botões da Home)

    readonly property string fontFamily: "Inter"
    readonly property int fontXs: 11       // text-[11px]
    readonly property int fontSm: 12       // text-xs
    readonly property int fontBase: 14     // text-sm (padrão do app)
    readonly property int fontMd: 16       // text-base
    readonly property int fontLg: 18       // text-lg
    readonly property int fontTiny: 10     // text-[10px] (selos)

    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacing: 16
    readonly property int spacingLg: 24
    readonly property int spacingXl: 32
    // Respiro das telas que não são o editor (p-4 lg:p-6).
    readonly property int pagePadding: 24
    readonly property int headerHeight: 40     // cabeçalho (sem a borda de 1px)

    readonly property int controlHeight: 40     // h-10 (Input, Button)
    readonly property int controlHeightSm: 36   // h-9
    readonly property int controlHeightLg: 44   // h-11

    readonly property int animationFast: 150
    readonly property int animationNormal: 300

    // ------------------------------------------------------------------ //
    // Utilitários
    // ------------------------------------------------------------------ //

    /// A mesma cor com outra opacidade (o "/40" do Tailwind).
    function alpha(c: color, a: real): color {
        return Qt.rgba(c.r, c.g, c.b, a);
    }

    /// "#rrggbb" sem alfa — o que o provider de ícones entende.
    function hex(c: color): string {
        function two(v) {
            const s = Math.round(v * 255).toString(16);
            return s.length < 2 ? "0" + s : s;
        }
        return "#" + two(c.r) + two(c.g) + two(c.b);
    }

    /// Ícone Lucide recolorido (image://icon/<nome>?color=%23rrggbb).
    function iconUrl(name: string, c: color): string {
        return "image://icon/" + name + "?color=" + encodeURIComponent(hex(c));
    }
}
