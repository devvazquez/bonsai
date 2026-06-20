import { File, Directory, Paths } from 'expo-file-system';
import * as Crypto from 'expo-crypto';
import { fetch } from 'expo/fetch';

interface TTSOptions {
  text: string;
  speed?: number;
  language?: string;
}

class TTSCache {
  constructor() {
    // Initialize cache directory
  }

  /**
   * Generate MD5 hash from text for cache key
   */
  private async generateCacheKey(text: string): Promise<string> {
    console.log('Generating cache key for text:', text);
    return await Crypto.digestStringAsync(
      Crypto.CryptoDigestAlgorithm.MD5,
      text
    );
  }

  /**
   * Get a File instance for a given cache key
   */
  private getCacheFile(cacheKey: string): File {
    return new File(Paths.cache, `${cacheKey}.mp3`);
  }

  /**
   * Check if file exists in cache
   */
  private isCached(file: File): boolean {
    return file.exists;
  }

  /**
   * Generate speech via API and save to cache, or return cached file if exists
   * Returns the file URI that can be used with AudioPlayer
   */
  async generateOrGetCached(options: TTSOptions): Promise<string> {
    const { text, speed = 1.0, language = 'ca' } = options;

    // Generate cache key
    const cacheKey = await this.generateCacheKey(text);
    const cacheFile = this.getCacheFile(cacheKey);

    // Check if cached file exists
    if (this.isCached(cacheFile)) {
      console.log('Using cached audio:', cacheFile.uri);
      return cacheFile.uri;
    }

    try {
      console.log('Generating new audio via TTS API...');

      // Call local TTS API endpoint
      const response = await fetch("http://192.168.5.31:8081/api/tts", {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({
          text: text,
          speed: speed,
        }),
      });
      
      console.log('TTS API response status:', response);
      if (!response.ok) {
        throw new Error(`TTS API error: ${response.statusText}`);
      }

      const uint8Array = await response.bytes();
      cacheFile.write(uint8Array);

      console.log('Audio saved to cache:', cacheFile.uri);
      return cacheFile.uri;
    } catch (error) {
      console.error('Error generating speech:', error);
      throw error;
    }
  }

  /**
   * Clear all cached audio files
   */
  clearCache(): void {
    try {
      const cacheDir = new Directory(Paths.cache);
      const files = cacheDir.list();
      for (const item of files) {
        if (item instanceof File && item.extension === '.mp3') {
          item.delete();
        }
      }
      console.log('Cache cleared');
    } catch (error) {
      console.error('Error clearing cache:', error);
    }
  }

  /**
   * Get cache size in bytes
   */
  getCacheSize(): number {
    try {
      const cacheDir = new Directory(Paths.cache);
      return cacheDir.size ?? 0;
    } catch (error) {
      console.error('Error getting cache size:', error);
      return 0;
    }
  }
}

// Export singleton instance
export default new TTSCache();
