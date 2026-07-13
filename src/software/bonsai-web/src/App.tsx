import * as React from "react"

import { Enso } from "@/components/enso"
import { MainPage } from "@/components/main-page"
import { ScanningScreen } from "@/components/scanning-screen"
import { Spinner } from "@/components/ui/spinner"

const SPLASH_DURATION_MS = 2000

function SplashScreen() {
  return (
    <div className="flex min-h-svh flex-col items-center justify-center gap-8">
      <div className="animate-in duration-700 fade-in zoom-in-95 motion-reduce:animate-none">
        <Enso state="open" className="size-36 text-primary">
          <span className="font-heading text-2xl font-medium">Bonsai</span>
        </Enso>
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
