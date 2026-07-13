import { Slider as SliderPrimitive } from "@base-ui/react/slider"

import { cn } from "@/lib/utils"

function Slider({
  className,
  "aria-label": ariaLabel,
  ...props
}: SliderPrimitive.Root.Props & { "aria-label"?: string }) {
  return (
    <SliderPrimitive.Root
      data-slot="slider"
      className={cn("w-full", className)}
      {...props}
    >
      <SliderPrimitive.Control className="flex w-full touch-none items-center py-2 select-none">
        <SliderPrimitive.Track className="h-1.5 w-full rounded-full bg-muted select-none">
          <SliderPrimitive.Indicator className="rounded-full bg-primary select-none" />
          <SliderPrimitive.Thumb
            aria-label={ariaLabel}
            className="block size-4 rounded-full bg-background shadow-sm ring-1 ring-foreground/20 outline-none select-none focus-visible:ring-3 focus-visible:ring-ring/50"
          />
        </SliderPrimitive.Track>
      </SliderPrimitive.Control>
    </SliderPrimitive.Root>
  )
}

export { Slider }
