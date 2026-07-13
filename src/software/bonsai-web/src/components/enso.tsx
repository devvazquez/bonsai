import { cn } from "@/lib/utils"

// The ensō ring is the app's status language: an open ring means "idle", a
// slowly turning open ring means "searching", and the ring closing means
// "connected". Drawn as one SVG stroke with pathLength=100 so dash values
// read as percentages. Color comes from currentColor.

type EnsoState = "open" | "spinning" | "closed"

type EnsoProps = {
  state?: EnsoState
  className?: string
  strokeWidth?: number
  children?: React.ReactNode
}

export function Enso({
  state = "open",
  className,
  strokeWidth = 4.5,
  children,
}: EnsoProps) {
  return (
    <div className={cn("relative", className)}>
      <svg
        viewBox="0 0 100 100"
        className="size-full"
        aria-hidden="true"
        focusable="false"
      >
        {/* Faint echo of the complete circle, like ink ghosting on paper. */}
        <circle
          cx="50"
          cy="50"
          r="45"
          fill="none"
          stroke="currentColor"
          strokeWidth={strokeWidth}
          className="opacity-15"
        />
        <g
          className={cn(
            "origin-center transform-fill",
            state === "spinning" &&
              "animate-spin animation-duration-[6s] motion-reduce:animate-none"
          )}
        >
          <circle
            cx="50"
            cy="50"
            r="45"
            fill="none"
            pathLength={100}
            stroke="currentColor"
            strokeWidth={strokeWidth}
            strokeLinecap="round"
            transform="rotate(-90 50 50)"
            strokeDasharray={state === "closed" ? "100 0" : "86 14"}
            className="transition-[stroke-dasharray] duration-700 ease-out motion-reduce:transition-none"
          />
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

type EnsoArcProps = {
  /** 0–100 */
  value: number
  className?: string
  children?: React.ReactNode
}

// A level read as a partial ensō — used for battery.
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
