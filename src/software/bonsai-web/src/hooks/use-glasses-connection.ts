import * as React from "react"

// Web Bluetooth is stubbed out for now so the app can be debugged in
// browsers without it (Safari). The hook keeps the same state shape as the
// real BLE implementation so it can be swapped back in later.
const DEVICE_NAME = "Bonsai"
const SIMULATED_SCAN_MS = 1800

type ConnectionState =
  | { status: "idle" }
  | { status: "scanning" }
  | { status: "connected"; deviceName: string }
  | { status: "error"; message: string }

export function useGlassesConnection() {
  const [state, setState] = React.useState<ConnectionState>({ status: "idle" })
  const timerRef = React.useRef<number | null>(null)

  React.useEffect(() => {
    return () => {
      if (timerRef.current !== null) {
        window.clearTimeout(timerRef.current)
      }
    }
  }, [])

  const scan = React.useCallback(() => {
    setState({ status: "scanning" })

    timerRef.current = window.setTimeout(() => {
      setState({ status: "connected", deviceName: DEVICE_NAME })
    }, SIMULATED_SCAN_MS)
  }, [])

  const disconnect = React.useCallback(() => {
    if (timerRef.current !== null) {
      window.clearTimeout(timerRef.current)
    }

    setState({ status: "idle" })
  }, [])

  return { state, scan, disconnect }
}
