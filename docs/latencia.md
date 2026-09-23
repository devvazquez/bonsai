# Latencia: qué se puede hacer con la cola de Groq

Groq responde rapidísimo, pero el tiempo hasta el primer token varía mucho: la
misma petición pasa de ~1 s a ~4 s sin que cambie nada del lado del dispositivo.
Eso es tiempo de planificación en la infraestructura de Groq — no se acorta desde
fuera. Solo hay tres ataques reales, y son complementarios.

## 1. Taparla (firmware)

El clip `start_talking` ya existe y se descarga al arrancar. Reproducirlo nada
más disparar la foto, mientras la petición está en vuelo, hace que el usuario
oiga algo a los ~200 ms **siempre**. Un "digue'm" de 1,5 s se come entera la cola
mediana y buena parte de la mala.

No reduce la latencia; elimina la percepción de latencia, que es lo que importa
cuando llevas las gafas puestas.

Detalle de implementación: `playWav()` coge GPIO8 (que es el MISO de la tarjeta)
y la tarjeta entera durante todo el clip, así que hay que ordenarlo con el
escritor de fotos en segundo plano y con `beginStream()` de la respuesta. La
forma que funciona es una tarea aparte que toma `sdLock`, carga el clip a PSRAM y
lo reproduce, mientras el hilo principal sube la imagen por el socket.

*Implementado en el flujo de `/ask`.*

## 2. Petición cubierta / hedging (backend)

En `bonsai-backend`: lanzar la llamada de visión y, si a los ~800 ms no ha
contestado, lanzar una segunda idéntica y quedarse con la que llegue antes,
cancelando la otra.

La cola es aleatoria por petición, así que dos tiradas independientes convierten
el p95 en algo parecido al p50. Se pagan tokens dobles solo en la cola lenta
(~10-20 % de las veces), y con el plan gratuito de Groq eso cuenta: ver el
apartado «Cuota de Groq» en el CLAUDE.md del backend antes de activarlo.

Variante más agresiva: lanzar en paralelo a dos proveedores distintos (Groq +
Cerebras, por ejemplo) y tomar el primero que conteste. Ahí el coste extra no es
de la misma cuota.

*Pendiente.*

## 3. Recortar lo que sí se controla

- **El cuerpo de `/look` va en base64 dentro de JSON**: son 33 % de bytes de más
  subiendo por un enlace de ~23 KB/s. `/ask` ya manda el JPEG binario
  (`[4 bytes de longitud][foto][audio]`), así que esta ya está ganada en el
  camino nuevo; `/look` sigue pagándola.
- **Bajar resolución/calidad de captura**: reduce subida *y* prefill. El backend
  ya reduce a 896 px antes de llamar a Groq (una foto de 3,1 MB son ~50.000
  tokens; a 896 px son 2.656), pero lo que sube el dispositivo lo paga el
  dispositivo.
- **Limitar la respuesta a 1-2 frases**: menos tokens generados y, como el TTS
  sale por frases, la primera se oye antes.
- **Handshake TLS caliente**: ya hecho (`warmUp()` al arrancar y `setReuse(true)`),
  vale más de un segundo en este chip.
- **Prebuffer de 400 ms** antes del primer sonido: ya está medido y ajustado al
  enlace real (8 kHz = 16 KB/s contra 21-30 KB/s de red). Ver las notas en
  `Audio.cpp`.

## Lo que ya no hay que volver a medir

- Groq contra Gemini para visión: 552 ms (551-554) contra 844 ms (649-937), y
  sobre todo mucho más regular. Decidido con datos, Gemini fuera.
- Piper contra edge-tts: 205 ms de mediana contra 1.320 ms con texto nuevo.
- La subida de la foto y la espera del backend se miden por separado en los logs
  (`[t] look: ...`), justamente para no confundir el problema de uno con el otro.
