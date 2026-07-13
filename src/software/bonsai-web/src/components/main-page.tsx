import * as React from "react"
import {
  BatteryMediumIcon,
  BellIcon,
  CameraIcon,
  CpuIcon,
  LanguagesIcon,
  MoonIcon,
  SignalIcon,
  SparklesIcon,
  SunIcon,
  SunMoonIcon,
  UnplugIcon,
  Volume2Icon,
} from "lucide-react"

import { EnsoArc, EnsoMark } from "@/components/enso"
import { useTheme } from "@/components/theme-provider"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import {
  Card,
  CardContent,
  CardDescription,
  CardFooter,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"
import { Slider } from "@/components/ui/slider"
import { Spinner } from "@/components/ui/spinner"
import { Switch } from "@/components/ui/switch"
import { useI18n, type Lang } from "@/lib/i18n"
import { cn } from "@/lib/utils"

// Lazy-loaded so three.js stays out of the initial bundle.
const GlassesViewer = React.lazy(() =>
  import("@/components/glasses-viewer").then((module) => ({
    default: module.GlassesViewer,
  }))
)

// Mocked telemetry until the BLE link is wired back in.
const MOCK_DEVICE = {
  batteryPercent: 82,
  firmwareVersion: "v0.1.0",
  camera: "OV2640",
}

type PhotoQuality = "low" | "medium" | "high"

type SegmentedOption<T extends string> = {
  value: T
  label: string
}

type SegmentedProps<T extends string> = {
  value: T
  onChange: (value: T) => void
  options: SegmentedOption<T>[]
  "aria-label": string
}

function Segmented<T extends string>({
  value,
  onChange,
  options,
  "aria-label": ariaLabel,
}: SegmentedProps<T>) {
  return (
    <div
      role="radiogroup"
      aria-label={ariaLabel}
      className="flex w-fit rounded-lg bg-muted p-0.5"
    >
      {options.map((option) => (
        <button
          key={option.value}
          type="button"
          role="radio"
          aria-checked={value === option.value}
          onClick={() => onChange(option.value)}
          className={cn(
            "min-h-9 rounded-md px-3 py-1.5 text-xs font-medium transition-colors outline-none focus-visible:ring-3 focus-visible:ring-ring/50",
            value === option.value
              ? "bg-background text-foreground shadow-sm ring-1 ring-foreground/10"
              : "text-muted-foreground hover:text-foreground"
          )}
        >
          {option.label}
        </button>
      ))}
    </div>
  )
}

type StatTileProps = {
  icon: React.ReactNode
  label: string
  value: string
  trailing?: React.ReactNode
  children?: React.ReactNode
}

function StatTile({ icon, label, value, trailing, children }: StatTileProps) {
  return (
    <Card size="sm" className="gap-2">
      <CardContent className="flex flex-col gap-2">
        <div className="flex items-center gap-1.5 text-muted-foreground">
          {icon}
          <span className="text-xs font-medium">{label}</span>
        </div>
        <div className="flex items-center justify-between gap-2">
          <span className="text-lg leading-none font-semibold tracking-tight">
            {value}
          </span>
          {trailing}
        </div>
        {children}
      </CardContent>
    </Card>
  )
}

type SettingRowProps = {
  icon: React.ReactNode
  title: string
  description: string
  children?: React.ReactNode
}

function SettingRow({ icon, title, description, children }: SettingRowProps) {
  return (
    <div className="flex min-h-11 items-center justify-between gap-4 py-3 first:pt-0 last:pb-0">
      <div className="flex items-center gap-3">
        <div className="flex size-8 shrink-0 items-center justify-center rounded-lg bg-muted text-muted-foreground [&_svg]:size-4">
          {icon}
        </div>
        <div className="flex flex-col gap-0.5">
          <span className="text-sm font-medium">{title}</span>
          <span className="text-xs text-muted-foreground">{description}</span>
        </div>
      </div>
      {children}
    </div>
  )
}

type RiseInProps = {
  delayMs?: number
  className?: string
  children: React.ReactNode
}

// One-time load-in: each section rises into place, slightly after the last.
function RiseIn({ delayMs = 0, className, children }: RiseInProps) {
  return (
    <div
      className={cn(
        "animate-in duration-500 fill-mode-backwards fade-in slide-in-from-bottom-2 motion-reduce:animate-none",
        className
      )}
      style={{ animationDelay: `${delayMs}ms` }}
    >
      {children}
    </div>
  )
}

type MainPageProps = {
  onDisconnect: () => void
}

export function MainPage({ onDisconnect }: MainPageProps) {
  const { theme, setTheme } = useTheme()
  const { lang, setLang, t } = useI18n()
  const [volume, setVolume] = React.useState(70)
  const [autoDescribe, setAutoDescribe] = React.useState(true)
  const [connectionSounds, setConnectionSounds] = React.useState(true)
  const [photoQuality, setPhotoQuality] = React.useState<PhotoQuality>("medium")

  return (
    <div className="min-h-svh bg-background">
      <header className="sticky top-0 z-10 border-b border-border/60 bg-background/80 backdrop-blur">
        <div className="mx-auto flex h-14 w-full max-w-md items-center justify-between px-4">
          <div className="flex items-center gap-2">
            <EnsoMark className="size-6 text-primary" />
            <span className="font-heading text-sm font-medium tracking-tight">
              Bonsai
            </span>
          </div>
          <Badge variant="success">
            <span className="relative flex size-2">
              <span className="absolute inline-flex h-full w-full animate-ping rounded-full bg-emerald-500 opacity-60 motion-reduce:animate-none" />
              <span className="relative inline-flex size-2 rounded-full bg-emerald-500" />
            </span>
            {t("main.connected")}
          </Badge>
        </div>
      </header>

      <main className="mx-auto flex w-full max-w-md flex-col gap-6 px-4 py-6 pb-12">
        <RiseIn className="flex flex-col items-center">
          <div className="h-60 w-full">
            <React.Suspense
              fallback={
                <div className="flex h-full items-center justify-center">
                  <Spinner className="size-6 text-muted-foreground" />
                </div>
              }
            >
              <GlassesViewer />
            </React.Suspense>
          </div>
          <span className="font-heading text-lg font-semibold tracking-tight">
            {t("main.deviceName")}
          </span>
          <span className="mt-1 text-[11px] font-medium tracking-widest text-muted-foreground uppercase">
            {t("main.revTag")}
          </span>
        </RiseIn>

        <RiseIn delayMs={100} className="flex flex-col gap-3">
          <h2 className="font-heading text-sm font-medium text-muted-foreground">
            {t("main.vitals")}
          </h2>
          <div className="grid grid-cols-2 gap-3">
            <StatTile
              icon={<BatteryMediumIcon className="size-3.5" />}
              label={t("main.battery")}
              value={`${MOCK_DEVICE.batteryPercent}%`}
              trailing={
                <EnsoArc
                  value={MOCK_DEVICE.batteryPercent}
                  className="size-9 text-primary"
                />
              }
            />
            <StatTile
              icon={<CpuIcon className="size-3.5" />}
              label={t("main.firmware")}
              value={MOCK_DEVICE.firmwareVersion}
            />
            <StatTile
              icon={<CameraIcon className="size-3.5" />}
              label={t("main.camera")}
              value={MOCK_DEVICE.camera}
            />
            <StatTile
              icon={<SignalIcon className="size-3.5" />}
              label={t("main.signal")}
              value={t("main.signalStrong")}
            />
          </div>
        </RiseIn>

        <RiseIn delayMs={200} className="flex flex-col gap-3">
          <h2 className="font-heading text-sm font-medium text-muted-foreground">
            {t("main.settings")}
          </h2>
          <Card>
            <CardHeader className="border-b">
              <CardTitle>{t("device.title")}</CardTitle>
              <CardDescription>{t("device.description")}</CardDescription>
            </CardHeader>
            <CardContent className="flex flex-col divide-y divide-border/60">
              <div className="flex flex-col gap-1 py-3 first:pt-0">
                <SettingRow
                  icon={<Volume2Icon />}
                  title={t("volume.title")}
                  description={t("volume.description")}
                >
                  <span className="text-sm font-medium tabular-nums text-muted-foreground">
                    {volume}%
                  </span>
                </SettingRow>
                <Slider
                  aria-label={t("volume.title")}
                  min={0}
                  max={100}
                  value={volume}
                  onValueChange={(value) =>
                    setVolume(Array.isArray(value) ? value[0] : value)
                  }
                />
              </div>
              <SettingRow
                icon={<SparklesIcon />}
                title={t("autoDescribe.title")}
                description={t("autoDescribe.description")}
              >
                <Switch
                  checked={autoDescribe}
                  onCheckedChange={setAutoDescribe}
                  aria-label={t("autoDescribe.title")}
                />
              </SettingRow>
              <SettingRow
                icon={<BellIcon />}
                title={t("sounds.title")}
                description={t("sounds.description")}
              >
                <Switch
                  checked={connectionSounds}
                  onCheckedChange={setConnectionSounds}
                  aria-label={t("sounds.title")}
                />
              </SettingRow>
              <SettingRow
                icon={<CameraIcon />}
                title={t("quality.title")}
                description={t("quality.description")}
              >
                <Segmented
                  aria-label={t("quality.title")}
                  value={photoQuality}
                  onChange={setPhotoQuality}
                  options={[
                    { value: "low", label: t("quality.low") },
                    { value: "medium", label: t("quality.medium") },
                    { value: "high", label: t("quality.high") },
                  ]}
                />
              </SettingRow>
              <SettingRow
                icon={
                  theme === "dark" ? (
                    <MoonIcon />
                  ) : theme === "light" ? (
                    <SunIcon />
                  ) : (
                    <SunMoonIcon />
                  )
                }
                title={t("appearance.title")}
                description={t("appearance.description")}
              >
                <Segmented
                  aria-label={t("appearance.title")}
                  value={theme}
                  onChange={setTheme}
                  options={[
                    { value: "light", label: t("theme.light") },
                    { value: "dark", label: t("theme.dark") },
                    { value: "system", label: t("theme.auto") },
                  ]}
                />
              </SettingRow>
              <SettingRow
                icon={<LanguagesIcon />}
                title={t("language.title")}
                description={t("language.description")}
              >
                <Segmented<Lang>
                  aria-label={t("language.title")}
                  value={lang}
                  onChange={setLang}
                  options={[
                    { value: "en", label: "EN" },
                    { value: "es", label: "ES" },
                    { value: "ca", label: "CA" },
                  ]}
                />
              </SettingRow>
            </CardContent>
            <CardFooter>
              <Button
                variant="destructive"
                className="h-12 w-full"
                onClick={onDisconnect}
              >
                <UnplugIcon data-icon="inline-start" />
                {t("disconnect")}
              </Button>
            </CardFooter>
          </Card>
        </RiseIn>
      </main>
    </div>
  )
}
