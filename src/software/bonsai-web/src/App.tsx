import * as React from "react"

import { MainPage } from "@/components/main-page"
import { ScanningScreen } from "@/components/scanning-screen"

export function App() {
  const [isConnected, setIsConnected] = React.useState(false)

  if (!isConnected) {
    return <ScanningScreen onConnected={() => setIsConnected(true)} />
  }

  return <MainPage onDisconnect={() => setIsConnected(false)} />
}

export default App
