/* eslint-disable react-refresh/only-export-components */
import * as React from "react"

export type Lang = "en" | "es" | "ca"

const STORAGE_KEY = "lang"
const LANG_VALUES: Lang[] = ["en", "es", "ca"]

const en = {
  "scan.titleIdle": "Connect your Bonsai",
  "scan.titleScanning": "Looking for your Bonsai…",
  "scan.titleConnected": "Connected",
  "scan.bodyIdle": "Power on your Bonsai and keep it near your phone.",
  "scan.bodyScanning": "Hang tight while we find your Bonsai nearby.",
  "scan.bodyConnected": "{name} is ready to go.",
  "scan.cta": "Scan for your Bonsai",
  "scan.ctaScanning": "Scanning…",
  "scan.note":
    "Bluetooth is off in this build — the connection is simulated for debugging.",
  "main.connected": "Connected",
  "main.deviceName": "Bonsai",
  "main.revTag": "Rev A prototype",
  "main.vitals": "Vitals",
  "main.battery": "Battery",
  "main.firmware": "Firmware",
  "main.camera": "Camera",
  "main.signal": "Signal",
  "main.signalStrong": "Strong",
  "main.settings": "Settings",
  "device.title": "Device",
  "device.description": "How your Bonsai captures and describes the world.",
  "volume.title": "Speaker volume",
  "volume.description": "Loudness of spoken descriptions.",
  "autoDescribe.title": "Auto describe",
  "autoDescribe.description": "Speak a description right after each photo.",
  "sounds.title": "Connection sounds",
  "sounds.description": "Play a chime when your Bonsai connects.",
  "quality.title": "Photo quality",
  "quality.description": "Higher quality takes longer to send.",
  "quality.low": "Low",
  "quality.medium": "Med",
  "quality.high": "High",
  "appearance.title": "Appearance",
  "appearance.description": "Theme for this app.",
  "theme.light": "Light",
  "theme.dark": "Dark",
  "theme.auto": "Auto",
  "language.title": "Language",
  "language.description": "Language for this app.",
  "disconnect": "Disconnect",
} as const

type MessageKey = keyof typeof en
type Messages = Record<MessageKey, string>

const es: Messages = {
  "scan.titleIdle": "Conecta tu Bonsai",
  "scan.titleScanning": "Buscando tu Bonsai…",
  "scan.titleConnected": "Conectado",
  "scan.bodyIdle": "Enciende tu Bonsai y mantenlo cerca del móvil.",
  "scan.bodyScanning": "Un momento, estamos buscando tu Bonsai.",
  "scan.bodyConnected": "{name} está listo.",
  "scan.cta": "Buscar mi Bonsai",
  "scan.ctaScanning": "Buscando…",
  "scan.note":
    "El Bluetooth está desactivado en esta versión: la conexión es simulada para depuración.",
  "main.connected": "Conectado",
  "main.deviceName": "Bonsai",
  "main.revTag": "Prototipo Rev A",
  "main.vitals": "Estado",
  "main.battery": "Batería",
  "main.firmware": "Firmware",
  "main.camera": "Cámara",
  "main.signal": "Señal",
  "main.signalStrong": "Fuerte",
  "main.settings": "Ajustes",
  "device.title": "Dispositivo",
  "device.description": "Cómo tu Bonsai captura y describe el mundo.",
  "volume.title": "Volumen del altavoz",
  "volume.description": "Volumen de las descripciones habladas.",
  "autoDescribe.title": "Descripción automática",
  "autoDescribe.description": "Describe cada foto justo después de hacerla.",
  "sounds.title": "Sonidos de conexión",
  "sounds.description": "Suena un aviso cuando tu Bonsai se conecta.",
  "quality.title": "Calidad de foto",
  "quality.description": "A más calidad, más tarda en enviarse.",
  "quality.low": "Baja",
  "quality.medium": "Media",
  "quality.high": "Alta",
  "appearance.title": "Apariencia",
  "appearance.description": "Tema de la aplicación.",
  "theme.light": "Claro",
  "theme.dark": "Oscuro",
  "theme.auto": "Auto",
  "language.title": "Idioma",
  "language.description": "Idioma de la aplicación.",
  "disconnect": "Desconectar",
}

const ca: Messages = {
  "scan.titleIdle": "Connecta el teu Bonsai",
  "scan.titleScanning": "Cercant el teu Bonsai…",
  "scan.titleConnected": "Connectat",
  "scan.bodyIdle": "Encén el teu Bonsai i mantén-lo a prop del mòbil.",
  "scan.bodyScanning": "Un moment, estem cercant el teu Bonsai.",
  "scan.bodyConnected": "{name} està a punt.",
  "scan.cta": "Cerca el meu Bonsai",
  "scan.ctaScanning": "Cercant…",
  "scan.note":
    "El Bluetooth està desactivat en aquesta versió: la connexió és simulada per a depuració.",
  "main.connected": "Connectat",
  "main.deviceName": "Bonsai",
  "main.revTag": "Prototip Rev A",
  "main.vitals": "Estat",
  "main.battery": "Bateria",
  "main.firmware": "Firmware",
  "main.camera": "Càmera",
  "main.signal": "Senyal",
  "main.signalStrong": "Fort",
  "main.settings": "Configuració",
  "device.title": "Dispositiu",
  "device.description": "Com el teu Bonsai captura i descriu el món.",
  "volume.title": "Volum de l'altaveu",
  "volume.description": "Volum de les descripcions parlades.",
  "autoDescribe.title": "Descripció automàtica",
  "autoDescribe.description": "Descriu cada foto just després de fer-la.",
  "sounds.title": "Sons de connexió",
  "sounds.description": "Sona un avís quan el teu Bonsai es connecta.",
  "quality.title": "Qualitat de foto",
  "quality.description": "Com més qualitat, més triga a enviar-se.",
  "quality.low": "Baixa",
  "quality.medium": "Mitjana",
  "quality.high": "Alta",
  "appearance.title": "Aparença",
  "appearance.description": "Tema de l'aplicació.",
  "theme.light": "Clar",
  "theme.dark": "Fosc",
  "theme.auto": "Auto",
  "language.title": "Llengua",
  "language.description": "Llengua de l'aplicació.",
  "disconnect": "Desconnecta",
}

const dictionaries: Record<Lang, Messages> = { en, es, ca }

function isLang(value: string | null): value is Lang {
  if (value === null) {
    return false
  }

  return LANG_VALUES.includes(value as Lang)
}

function detectLang(): Lang {
  const stored = localStorage.getItem(STORAGE_KEY)
  if (isLang(stored)) {
    return stored
  }

  for (const tag of navigator.languages ?? [navigator.language]) {
    const base = tag.toLowerCase().split("-")[0]
    if (isLang(base)) {
      return base
    }
  }

  return "en"
}

type I18nState = {
  lang: Lang
  setLang: (lang: Lang) => void
  t: (key: MessageKey, vars?: Record<string, string>) => string
}

const I18nContext = React.createContext<I18nState | undefined>(undefined)

export function LanguageProvider({ children }: { children: React.ReactNode }) {
  const [lang, setLangState] = React.useState<Lang>(detectLang)

  const setLang = React.useCallback((nextLang: Lang) => {
    localStorage.setItem(STORAGE_KEY, nextLang)
    setLangState(nextLang)
  }, [])

  React.useEffect(() => {
    document.documentElement.lang = lang
  }, [lang])

  const t = React.useCallback(
    (key: MessageKey, vars?: Record<string, string>) => {
      let message: string = dictionaries[lang][key]
      if (vars) {
        for (const [name, value] of Object.entries(vars)) {
          message = message.replace(`{${name}}`, value)
        }
      }
      return message
    },
    [lang]
  )

  const value = React.useMemo(() => ({ lang, setLang, t }), [lang, setLang, t])

  return <I18nContext.Provider value={value}>{children}</I18nContext.Provider>
}

export function useI18n() {
  const context = React.useContext(I18nContext)

  if (context === undefined) {
    throw new Error("useI18n must be used within a LanguageProvider")
  }

  return context
}
