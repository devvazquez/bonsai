import * as React from "react"
import { Canvas, useFrame, useLoader } from "@react-three/fiber"
import { ContactShadows, Float } from "@react-three/drei"
import { STLLoader } from "three/addons/loaders/STLLoader.js"
import * as BufferGeometryUtils from "three/addons/utils/BufferGeometryUtils.js"
import type { Group } from "three"

const MODEL_URL = "/models/product.stl"
const MODEL_COLOR = "#2b2b31"
// Widest dimension of the model in scene units, whatever the CAD export units.
const TARGET_SIZE = 2.8

function GlassesModel() {
  const rawGeometry = useLoader(STLLoader, MODEL_URL)

  const { geometry, scale } = React.useMemo(() => {
    // STL triangles are disconnected; re-derive smooth normals but keep
    // hard edges past the crease angle so the CAD shape reads correctly.
    const merged = BufferGeometryUtils.toCreasedNormals(rawGeometry, Math.PI / 5)
    merged.center()
    merged.computeBoundingBox()
    const box = merged.boundingBox!
    const maxDimension = Math.max(
      box.max.x - box.min.x,
      box.max.y - box.min.y,
      box.max.z - box.min.z
    )
    return { geometry: merged, scale: TARGET_SIZE / maxDimension }
  }, [rawGeometry])

  return (
    // CAD exports are Z-up; rotate to three.js Y-up.
    <mesh geometry={geometry} scale={scale} rotation={[-Math.PI / 2, 0, 0]}>
      <meshStandardMaterial color={MODEL_COLOR} roughness={0.4} metalness={0.15} />
    </mesh>
  )
}

function SpinningGlasses() {
  const groupRef = React.useRef<Group>(null)

  useFrame((_, delta) => {
    if (groupRef.current) {
      groupRef.current.rotation.y += delta * 0.25
    }
  })

  return (
    <group ref={groupRef}>
      <GlassesModel />
    </group>
  )
}

// Non-interactive product visualization: the model spins slowly on its own
// and the canvas ignores pointer events so it blends into the page.
export function GlassesViewer() {
  return (
    <Canvas
      camera={{ position: [2.8, 1.2, 5.2], fov: 35 }}
      dpr={[1, 2]}
      gl={{ alpha: true, antialias: true }}
      style={{ pointerEvents: "none" }}
      onCreated={({ camera }) => camera.lookAt(0, 0, 0)}
    >
      <ambientLight intensity={1.1} />
      <directionalLight position={[4, 6, 4]} intensity={2} />
      <directionalLight position={[-5, 3, -4]} intensity={1} />
      <React.Suspense fallback={null}>
        <Float speed={1.6} rotationIntensity={0.25} floatIntensity={0.5}>
          <SpinningGlasses />
        </Float>
      </React.Suspense>
      <ContactShadows position={[0, -1, 0]} opacity={0.35} scale={7} blur={2.6} far={2} />
    </Canvas>
  )
}
