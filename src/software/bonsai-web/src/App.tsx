import * as React from "react"
import { GlassesIcon } from "lucide-react"

import { Enso } from "@/components/enso"
import { MainPage } from "@/components/main-page"
import { ScanningScreen } from "@/components/scanning-screen"
import { Spinner } from "@/components/ui/spinner"

const SPLASH_DURATION_MS = 2000

function SplashScreen() {
  return (
    <div className="flex min-h-svh flex-col items-center justify-center gap-6">
      <div className="flex animate-in flex-col items-center gap-4 duration-700 fade-in zoom-in-95 motion-reduce:animate-none">
        <Enso state="open" className="size-28 text-primary/60">
          <div className="flex size-14 items-center justify-center rounded-2xl bg-primary text-primary-foreground shadow-lg">
            <GlassesIcon className="size-7" />
          </div>
        </Enso>
        <span className="font-heading text-lg font-semibold tracking-tight">
          Bonsai
        </span>
      </div>
      <Spinner className="size-5 text-muted-foreground" />
    </div>
  )
}

export function App() {
  const [isLoading, setIsLoading] = React.useState(true)
  const [isConnected, setIsConnected] = React.useState(false)

  React.useEffect(() => {
    const timer = window.setTimeout(() => setIsLoading(false), SPLASH_DURATION_MS)
    return () => window.clearTimeout(timer)
  }, [])

  if (isLoading) {
    return <SplashScreen />
  }

  if (!isConnected) {
    return <ScanningScreen onConnected={() => setIsConnected(true)} />
  }

  return <MainPage onDisconnect={() => setIsConnected(false)} />
}

export default App
