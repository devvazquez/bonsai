# hey_bonsai — muestras sintéticas (Piper TTS)

300 clips de la wake word **"Hey Bonsai"** generados con Piper TTS y
post-procesados para parecerse a grabaciones reales de micrófono.

**Estos clips son un complemento del dataset grabado a mano, no un sustituto.**

## Formato

Idéntico al que consume el XIAO ESP32S3 Sense:

| Propiedad | Valor |
| --- | --- |
| Sample rate | 16 000 Hz |
| Canales | 1 (mono) |
| Codificación | PCM 16-bit |
| Duración | 1000 ms exactos |

## Cómo se generaron

- **7 voces Piper** (2 de ellas multi-speaker): `en_US-amy`, `en_US-ryan`,
  `en_US-lessac`, `en_GB-alba`, `es_ES-davefx`, `es_ES-sharvard`, `ca_ES-upc_ona`.
- Parámetros de síntesis aleatorios por muestra: `length_scale` 0.85–1.20,
  `noise_scale` 0.55–0.80, `noise_w_scale` 0.60–0.95.
- Post-proceso: resample a 16 kHz mono, trim de silencios, pitch-shift de
  ±1–2 semitonos en ~50 % de las muestras, mezcla de ruido (blanco/rosa, o
  clips de `./noise_samples/` si existen) a SNR aleatorio 15–25 dB, centrado a
  1000 ms con relleno de ruido de fondo bajo (nunca silencio digital) y
  normalización con ganancia aleatoria de −6 a +3 dB, sin clipping.

`metadata.csv` / `metadata.json` registran voz, idioma, speaker, frase y todos
los parámetros usados en cada archivo.

Regenerar (o cambiar el número de muestras):

```bash
python3 tools/gen_wakeword_synth.py --count 300
```

## Cuánto de esto subir

Manténlo en **≤ 25 % del total** de la clase `hey_bonsai`. Con `R` clips reales:

```
N_sintéticos_máx = 0.33 × R      # 0.25/(1-0.25)
```

Ejemplo: 200 clips reales → sube como mucho ~66 sintéticos. Si necesitas menos
de 300, coge un subconjunto **balanceado por voz** filtrando `metadata.csv` por
la columna `voice`, no los primeros N por nombre de archivo.

## Subir a Edge Impulse

1. Abre tu proyecto → **Data acquisition** → botón **Upload data**.
2. **Select files**: los `.wav` de esta carpeta (no subas los `metadata.*` ni
   este README).
3. **Upload into category**: `Training`. Deja fuera del test set los
   sintéticos — el set de test debe ser 100 % voz real, o las métricas
   mentirán sobre el rendimiento en el dispositivo.
4. **Label**: `Enter label` → escribe exactamente `hey_bonsai`.
5. **Upload**. Luego revisa el balance de clases y re-ejecuta
   **Create impulse → MFCC → Classifier**.

Los nombres ya son únicos (`hey_bonsai_synth_{idx}_{voz}.wav`), así que no
colisionan con los clips grabados.

## Comprobación después de entrenar

Compara la accuracy sobre el test set **real** antes y después de añadir
estos clips. Si sube el train y baja el test real, has metido demasiado
sintético: reduce la proporción.
