import * as React from "react"
import { BluetoothSearchingIcon } from "lucide-react"

import { Enso, EnsoMark } from "@/components/enso"
import { Button } from "@/components/ui/button"
import { useGlassesConnection } from "@/hooks/use-glasses-connection"
import { useI18n } from "@/lib/i18n"
import { cn } from "@/lib/utils"

const CONNECTED_HOLD_MS = 900

type ScanningScreenProps = {
  onConnected: () => void
}

export function ScanningScreen({ onConnected }: ScanningScreenProps) {
  const { state, scan } = useGlassesConnection()
  const { t } = useI18n()

  React.useEffect(() => {
    if (state.status !== "connected") {
      return
    }

    const timer = window.setTimeout(onConnected, CONNECTED_HOLD_MS)
    return () => window.clearTimeout(timer)
  }, [state.status, onConnected])

  const isScanning = state.status === "scanning"
  const isConnected = state.status === "connected"

  return (
    <div className="mx-auto flex min-h-svh w-full max-w-md flex-col items-center justify-between gap-8 p-6">
      <div className="flex items-center gap-2 pt-2">
        <EnsoMark className="size-6 text-primary" />
        <span className="font-heading text-sm font-medium tracking-tight">
          Bonsai
        </span>
      </div>

      <div className="flex w-full max-w-sm flex-col items-center gap-8 text-center">
        <Enso
          state={isConnected ? "closed" : isScanning ? "spinning" : "open"}
          className={cn(
            "size-40 transition-colors duration-500",
            isConnected
              ? "text-emerald-500"
              : isScanning
                ? "text-primary"
                : "text-muted-foreground/60"
          )}
        />

        <div className="flex flex-col gap-2">
          <h1 className="font-heading text-xl font-semibold tracking-tight">
            {isConnected
              ? t("scan.titleConnected")
              : isScanning
                ? t("scan.titleScanning")
                : t("scan.titleIdle")}
          </h1>
          <p className="mx-auto max-w-xs text-sm text-balance text-muted-foreground">
            {isConnected && state.status === "connected"
              ? t("scan.bodyConnected", { name: state.deviceName })
              : isScanning
                ? t("scan.bodyScanning")
                : t("scan.bodyIdle")}
          </p>
        </div>
      </div>

      <div className="flex w-full max-w-sm flex-col items-center gap-4 pb-2">
        <Button
          size="lg"
          className={cn(
            "h-13 w-full rounded-full text-base",
            isConnected && "invisible"
          )}
          onClick={scan}
          disabled={isScanning}
        >
          <BluetoothSearchingIcon data-icon="inline-start" className="size-5" />
          {isScanning ? t("scan.ctaScanning") : t("scan.cta")}
        </Button>
        <p className="max-w-xs text-center text-xs text-muted-foreground/70">
          {t("scan.note")}
        </p>
      </div>
    </div>
  )
}
