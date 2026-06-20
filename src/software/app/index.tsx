import React, { useEffect, useRef } from 'react';
import { View, StyleSheet, Text, TouchableOpacity, Animated } from 'react-native';
import { CameraView, useCameraPermissions } from 'expo-camera';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { MaterialIcons } from '@expo/vector-icons';
import { Colors, Fonts, BorderRadius } from '../constants/theme';
import Header from '../components/Header';
import { fetch } from 'expo/fetch';
import TTSCache from '../services/TTSCache';
import { useAudioPlayer } from 'expo-audio';

const GEMINI_API_KEY = process.env.EXPO_PUBLIC_GEMINI_API_KEY;

export default function LiveFeedScreen() {
  const insets = useSafeAreaInsets();
  const [permission, requestPermission] = useCameraPermissions();
  const pulseAnim = useRef(new Animated.Value(1)).current;
  const cameraRef = useRef<CameraView>(null);
  const intervalRef = useRef<NodeJS.Timeout | null>(null);
  const audioPlayer = useAudioPlayer(null);

  // Prevent overlapping API calls
  const isProcessingRef = useRef(false);

  useEffect(() => {
    Animated.loop(
      Animated.sequence([
        Animated.timing(pulseAnim, {
          toValue: 0.4,
          duration: 700,
          useNativeDriver: true,
        }),
        Animated.timing(pulseAnim, {
          toValue: 1,
          duration: 700,
          useNativeDriver: true,
        }),
      ])
    ).start();
  }, []);

  useEffect(() => {
    if (!permission?.granted) {
      requestPermission();
    }
  }, [permission]);

  // Capture frame every 5 seconds
  useEffect(() => {
    if (!permission?.granted || !cameraRef.current) return;

    const captureFrame = async () => {
      try {
        const photo = await cameraRef.current?.takePictureAsync({
          base64: true,
          quality: 0.5, // optimized
        });

        if (photo?.base64) {
          await onFrameCaptured(photo.base64);
        }
      } catch (error) {
        console.error('Error capturing frame:', error);
      }
    };

    intervalRef.current = setInterval(captureFrame, 15000);

    return () => {
      if (intervalRef.current) {
        clearInterval(intervalRef.current);
      }
    };
  }, [permission?.granted]);

  const onFrameCaptured = async (base64Image: string) => {
    if (isProcessingRef.current) return;
    isProcessingRef.current = true;
    console.log('Frame captured, sending to Gemini...');
    try {
          const payload = {
            "model": `microsoft/phi-4-multimodal-instruct`,
            "messages": [
              {
                "role": "user",
                "content": `Describe esta imagen en castellano en un máximo de 2 líneas: <img src=\"data:image/png;base64,${base64Image}\" />`
              }
            ],
            "max_tokens": 512,
            "temperature": 0.10,
            "top_p": 0.70,
            "frequency_penalty": 0.00,
            "presence_penalty": 0.00,
          };

      const response = await fetch(
        "https://integrate.api.nvidia.com/v1/chat/completions",
        {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json', 
            "Authorization": "Bearer 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",
          },
          body: JSON.stringify(payload),
        }
      );

      const data = await response.json();
      console.log('Gemini response:', data);
      const caption =
        data?.choices?.[0]?.message?.content;

      if (caption) {
        console.log('Caption:', caption);
        // Get or generate audio from cache
        const uri = await TTSCache.generateOrGetCached({
          text: caption
        });
        
        audioPlayer.replace(uri);
        audioPlayer.play();
        
        await new Promise((resolve) => {
          audioPlayer.addListener('playbackStatusUpdate', (status) => {
            if(status.didJustFinish){
              resolve(null);
            }
          });
        });
      } else {
        console.log('No caption returned');
      }
    } catch (error) {
      console.error('Error processing frame:', error);
    } finally {
      isProcessingRef.current = false;
    }
  };

  return (
    <View style={[styles.container, { paddingTop: insets.top }]}>
      <Header />

      <View style={styles.feedContainer}>
        {permission?.granted ? (
          <CameraView
            ref={cameraRef}
            style={styles.camera}
            facing="back"
            zoom={0.14}
          />
        ) : (
          <View style={styles.permissionContainer}>
            <MaterialIcons name="videocam-off" size={48} color={Colors.outlineVariant} />
            <Text style={styles.permissionText}>Camera access needed</Text>
            <TouchableOpacity style={styles.permissionButton} onPress={requestPermission}>
              <Text style={styles.permissionButtonText}>Grant Access</Text>
            </TouchableOpacity>
          </View>
        )}

        {permission?.granted && (
          <View style={styles.overlay} pointerEvents="box-none">
            <View style={styles.statusRow}>
              <View style={styles.statusRight}>
                <View style={styles.statusPill}>
                  <Animated.View style={[styles.greenDot, { opacity: pulseAnim }]} />
                  <Text style={styles.statusText}>CONNECTED</Text>
                </View>
                <View style={styles.statusPill}>
                  <MaterialIcons name="wifi" size={14} color={Colors.white} />
                  <Text style={styles.statusText}>Stable</Text>
                </View>
              </View>

              <View style={styles.statusPill}>
                <MaterialIcons name="battery-full" size={14} color={Colors.white} />
                <Text style={styles.statusText}>88%</Text>
              </View>
            </View>
          </View>
        )}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: Colors.background,
  },
  feedContainer: {
    flex: 1,
    position: 'relative',
    overflow: 'hidden',
    backgroundColor: Colors.surfaceContainerHighest,
  },
  camera: {
    flex: 1,
  },
  permissionContainer: {
    flex: 1,
    justifyContent: 'center',
    alignItems: 'center',
    backgroundColor: Colors.surfaceContainerHighest,
    gap: 16,
  },
  permissionText: {
    fontSize: 14,
    color: Colors.outlineVariant,
  },
  permissionButton: {
    backgroundColor: Colors.primary,
    paddingHorizontal: 24,
    paddingVertical: 12,
    borderRadius: 24,
  },
  permissionButtonText: {
    fontSize: 14,
    color: Colors.white,
  },
  overlay: {
    ...StyleSheet.absoluteFillObject,
    padding: 24,
    justifyContent: 'flex-start',
  },
  statusRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'flex-start',
  },
  statusRight: {
    alignItems: 'flex-start',
    gap: 8,
  },
  statusPill: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
    paddingHorizontal: 14,
    paddingVertical: 8,
    backgroundColor: 'rgba(15, 23, 42, 0.4)',
    borderRadius: BorderRadius.full,
    borderWidth: 1,
    borderColor: 'rgba(255, 255, 255, 0.1)',
  },
  greenDot: {
    width: 8,
    height: 8,
    borderRadius: 4,
    backgroundColor: '#34d399',
  },
  statusText: {
    fontFamily: Fonts.labelBold,
    fontSize: 11,
    color: Colors.white,
    letterSpacing: 2,
    textTransform: 'uppercase',
  },
});