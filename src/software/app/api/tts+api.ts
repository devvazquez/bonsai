import { ElevenLabsClient } from '@elevenlabs/elevenlabs-js';

export async function POST(req: Request) {
  try {
    const { text, speed = 1.0, language = 'en-US' } = await req.json();
    console.log('TTS request received:', { text, speed, language });
    if (!text) {
      return Response.json(
        { error: 'Text is required' },
        { status: 400 }
      );
    }

    console.log('TTS request received:', { text, speed, language });

    // Initialize ElevenLabs client with API key from environment
    const elevenlabs = new ElevenLabsClient({
      apiKey: process.env.ELEVENLABS_API_KEY,
    });

    // Convert text to speech
    const audioStream = await elevenlabs.textToSpeech.convert(
      'onwK4e9ZLuTAKqWW03F9', // "George" voice
      {
        text: text,
        modelId: 'eleven_v3',
        outputFormat: 'mp3_44100_128',
        languageCode: "es",
      }
    );

    // Convert ReadableStream to Buffer
    const reader = audioStream.getReader();
    const chunks: Uint8Array[] = [];

    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value);
    }

    const buffer = Buffer.concat(chunks);

    // Return audio buffer
    return new Response(buffer, {
      headers: {
        'Content-Type': 'audio/mpeg',
      },
    });
  } catch (error) {
    console.error('TTS API error:', error);
    return Response.json(
      { error: 'Failed to generate speech', details: String(error) },
      { status: 500 }
    );
  }
}
