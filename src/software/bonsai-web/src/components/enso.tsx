import * as React from "react"

import { cn } from "@/lib/utils"

// The ensō ring is the app's status language: an open ring means "idle", a
// slowly turning open ring means "searching", and the ring closing means
// "connected". It doubles as the brand mark now that the device is a
// clip-on, not a pair of glasses.
//
// The brush look is built from three stacked arcs of decreasing length and
// increasing width (the ends step down in thickness, reading as a tapered
// stroke), a tonal gradient along the ink, and a touch of turbulence so the
// edge isn't geometrically perfect. Dash values are percentages via
// pathLength=100. Color comes from currentColor.

type EnsoState = "open" | "spinning" | "closed"

type EnsoProps = {
  state?: EnsoState
  className?: string
  strokeWidth?: number
  children?: React.ReactNode
}

// dash: "visible gap"; offset centers shorter arcs inside the longest one.
const BRUSH_LAYERS = [
  { width: 0.5, dash: "88 12", offset: 0, opacity: 0.9 },
  { width: 0.8, dash: "82 18", offset: -3, opacity: 0.95 },
  { width: 1, dash: "74 26", offset: -7, opacity: 1 },
]

export function Enso({
  state = "open",
  className,
  strokeWidth = 5,
  children,
}: EnsoProps) {
  const uid = React.useId().replace(/[^a-zA-Z0-9-]/g, "")
  const inkId = `enso-ink-${uid}`
  const brushId = `enso-brush-${uid}`

  return (
    <div className={cn("relative", className)}>
      <svg
        viewBox="0 0 100 100"
        className="size-full"
        aria-hidden="true"
        focusable="false"
      >
        <defs>
          <linearGradient
            id={inkId}
            x1="0"
            y1="0"
            x2="1"
            y2="1"
          >
            <stop offset="0" stopColor="currentColor" stopOpacity="1" />
            <stop offset="0.55" stopColor="currentColor" stopOpacity="0.85" />
            <stop offset="1" stopColor="currentColor" stopOpacity="0.5" />
          </linearGradient>
          <filter id={brushId} x="-15%" y="-15%" width="130%" height="130%">
            <feTurbulence
              type="fractalNoise"
              baseFrequency="0.9"
              numOctaves="2"
              seed="7"
              result="noise"
            />
            <feDisplacementMap in="SourceGraphic" in2="noise" scale="2.4" />
          </filter>
        </defs>

        {/* Faint echo of the complete circle, like ink ghosting on paper. */}
        <circle
          cx="50"
          cy="50"
          r="45"
          fill="none"
          stroke="currentColor"
          strokeWidth={strokeWidth * 0.7}
          filter={`url(#${brushId})`}
          className="opacity-10"
        />

        <g
          className={cn(
            "origin-center transform-fill",
            state === "spinning" &&
              "animate-spin animation-duration-[6s] motion-reduce:animate-none"
          )}
        >
          <g filter={`url(#${brushId})`}>
            {BRUSH_LAYERS.map((layer) => (
              <circle
                key={layer.dash}
                cx="50"
                cy="50"
                r="45"
                fill="none"
                pathLength={100}
                stroke={`url(#${inkId})`}
                strokeWidth={strokeWidth * layer.width}
                strokeLinecap="round"
                strokeOpacity={layer.opacity}
                transform="rotate(-90 50 50)"
                strokeDasharray={state === "closed" ? "100 0" : layer.dash}
                strokeDashoffset={layer.offset}
                className="transition-[stroke-dasharray] duration-700 ease-out motion-reduce:transition-none"
              />
            ))}
          </g>
        </g>
      </svg>
      {children ? (
        <div className="absolute inset-0 flex items-center justify-center">
          {children}
        </div>
      ) : null}
    </div>
  )
}

// The small brand mark: a static open ensō, thick enough to read at 20px.
export function EnsoMark({ className }: { className?: string }) {
  return <Enso state="open" strokeWidth={11} className={className} />
}

type EnsoArcProps = {
  /** 0–100 */
  value: number
  className?: string
  children?: React.ReactNode
}

// A level read as a partial ensō — used for battery. Stays a single clean
// arc so the quantity reads precisely.
export function EnsoArc({ value, className, children }: EnsoArcProps) {
  const clamped = Math.max(0, Math.min(100, value))

  return (
    <div className={cn("relative", className)}>
      <svg
        viewBox="0 0 100 100"
        className="size-full"
        aria-hidden="true"
        focusable="false"
      >
        <circle
          cx="50"
          cy="50"
          r="40"
          fill="none"
          stroke="currentColor"
          strokeWidth={11}
          className="opacity-15"
        />
        <circle
          cx="50"
          cy="50"
          r="40"
          fill="none"
          pathLength={100}
          stroke="currentColor"
          strokeWidth={11}
          strokeLinecap="round"
          transform="rotate(-90 50 50)"
          strokeDasharray={`${clamped} ${100 - clamped}`}
        />
      </svg>
      {children ? (
        <div className="absolute inset-0 flex items-center justify-center">
          {children}
        </div>
      ) : null}
    </div>
  )
}
