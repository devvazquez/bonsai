import * as React from "react"
import {
  BluetoothSearchingIcon,
  CircleCheckIcon,
  GlassesIcon,
} from "lucide-react"

import { Enso } from "@/components/enso"
import { Button } from "@/components/ui/button"
import { useGlassesConnection } from "@/hooks/use-glasses-connection"
import { cn } from "@/lib/utils"

const CONNECTED_HOLD_MS = 900

type ScanningScreenProps = {
  onConnected: () => void
}

export function ScanningScreen({ onConnected }: ScanningScreenProps) {
  const { state, scan } = useGlassesConnection()

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
        <div className="flex size-7 items-center justify-center rounded-md bg-primary text-primary-foreground">
          <GlassesIcon className="size-4" />
        </div>
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
                : "text-muted-foreground/50"
          )}
        >
          <div
            className={cn(
              "flex size-16 items-center justify-center rounded-2xl shadow-lg transition-colors duration-500",
              isConnected
                ? "bg-emerald-500 text-white"
                : "bg-primary text-primary-foreground"
            )}
          >
            {isConnected ? (
              <CircleCheckIcon className="size-8" />
            ) : (
              <GlassesIcon className="size-8" />
            )}
          </div>
        </Enso>

        <div className="flex flex-col gap-2">
          <h1 className="font-heading text-xl font-semibold tracking-tight">
            {isConnected
              ? "Connected"
              : isScanning
                ? "Looking for your glasses…"
                : "Connect your glasses"}
          </h1>
          <p className="mx-auto max-w-xs text-sm text-balance text-muted-foreground">
            {isConnected && state.status === "connected"
              ? `${state.deviceName} is ready to go.`
              : isScanning
                ? "Hang tight while we find your Bonsai glasses nearby."
                : "Turn on your Bonsai glasses and keep them close to this device."}
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
          {isScanning ? "Scanning…" : "Scan for glasses"}
        </Button>
        <p className="max-w-xs text-center text-xs text-muted-foreground/70">
          Bluetooth is off in this build — the connection is simulated for
          debugging.
        </p>
      </div>
    </div>
  )
}
