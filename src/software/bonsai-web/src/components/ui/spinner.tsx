import { cn } from "@/lib/utils"
import { LoaderIcon } from "lucide-react"

function Spinner({ className, style, ...props }: React.ComponentProps<"svg">) {
  return (
        <LoaderIcon
          role="status"
          aria-label="Loading"
          className={cn("size-4 animate-spin", className)}
          style={{ animationDuration: "2s", ...style }}
          {...props}
        />
  )
}

export { Spinner }
