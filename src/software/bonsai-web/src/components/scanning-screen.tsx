import * as React from "react"
import {
  BluetoothSearchingIcon,
  CircleCheckIcon,
  GlassesIcon,
} from "lucide-react"

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
    <div className="flex min-h-svh flex-col items-center justify-between gap-8 p-6">
      <div className="flex items-center gap-2 pt-2">
        <div className="flex size-7 items-center justify-center rounded-md bg-primary text-primary-foreground">
          <GlassesIcon className="size-4" />
        </div>
        <span className="font-heading text-sm font-medium tracking-tight">
          Bonsai
        </span>
      </div>

      <div className="flex w-full max-w-sm flex-col items-center gap-8 text-center">
        <div className="relative flex size-32 items-center justify-center">
          {isScanning ? (
            <>
              <span className="absolute inset-0 animate-ping rounded-full bg-primary/10 animation-duration-[2s]" />
              <span className="absolute inset-4 animate-ping rounded-full bg-primary/15 animation-duration-[2s] animation-delay-[400ms]" />
            </>
          ) : null}
          <div
            className={cn(
              "relative flex size-16 items-center justify-center rounded-2xl shadow-lg transition-colors duration-500",
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
        </div>

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

        {!isConnected ? (
          <Button
            size="lg"
            className="w-full max-w-60"
            onClick={scan}
            disabled={isScanning}
          >
            <BluetoothSearchingIcon data-icon="inline-start" />
            {isScanning ? "Scanning…" : "Scan for glasses"}
          </Button>
        ) : null}
      </div>

      <p className="max-w-xs pb-2 text-center text-xs text-muted-foreground/70">
        Bluetooth is disabled in this build — the connection is simulated for
        debugging.
      </p>
    </div>
  )
}
