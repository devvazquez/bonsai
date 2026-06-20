import React, { useState } from 'react';
import {
  View,
  Text,
  StyleSheet,
  ScrollView,
  TouchableOpacity,
  Platform,
} from 'react-native';
import Slider from '@react-native-community/slider';
import { MaterialIcons } from '@expo/vector-icons';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import Header from '../components/Header';
import { Colors, Fonts, BorderRadius, Shadows, Spacing } from '../constants/theme';
import { useAudioPlayer, setAudioModeAsync } from 'expo-audio';
import TTSCache from '../services/TTSCache';

const QUALITY_OPTIONS = ['ECO', 'HD', 'ULTRA'] as const;
const LANGUAGES = ['English (US)', 'Japanese (JP)', 'French (FR)', 'German (DE)'] as const;
const SAMPLE_TEXT = 'This is how your glasses will sound.';


export default function SettingsScreen() {
  const insets = useSafeAreaInsets();
  const [volume, setVolume] = useState(75);
  const [voiceSpeed, setVoiceSpeed] = useState(1.0);
  const [imageQuality, setImageQuality] = useState<typeof QUALITY_OPTIONS[number]>('ECO');
  const [language, setLanguage] = useState(0);
  const [isPlayingSample, setIsPlayingSample] = useState(false);

  const player = useAudioPlayer(null, { downloadFirst: true }); // Initialize audio player without a source
  setAudioModeAsync({
    playsInSilentMode: true
  })

  const handlePlaySample = async () => {
    setIsPlayingSample(true);
    
    try {
      // Get or generate audio from cache
      const uri = await TTSCache.generateOrGetCached({
        text: SAMPLE_TEXT,
        speed: voiceSpeed,
        language: LANGUAGES[language],
      });


      // Load and play the audio
      player.replace(uri);
      player.seekTo(0);
      player.play();

      // Wait for audio to finish playback
      await new Promise((resolve) => {
        player.addListener('playbackStatusUpdate', (status) => {
          if(status.didJustFinish){
            resolve(null);
          }
        });
      });
      
    } catch (error) {
      console.error('Failed to play sample:', error);
    } finally {
      setIsPlayingSample(false);
    }
  };

  return (
    <View style={[styles.container, { paddingTop: insets.top }]}>
      <Header />
      <ScrollView
        style={styles.scroll}
        contentContainerStyle={styles.scrollContent}
        showsVerticalScrollIndicator={false}
      >
        {/* Header Section */}
        <View style={styles.headerSection}>
          <Text style={styles.pageTitle}>Settings</Text>
          <Text style={styles.pageSubtitle}>Fine-tune your spatial experience.</Text>
        </View>

        {/* Audio & Voice Cards */}
        <View style={styles.audioGrid}>
          {/* Volume Card */}
          <View style={styles.audioCard}>
            <View style={styles.audioCardHeader}>
              <View style={styles.iconBubble}>
                <MaterialIcons name="volume-up" size={20} color={Colors.primary} />
              </View>
              <Text style={styles.audioCardLabel}>AUDIO</Text>
            </View>
            <View>
              <Text style={styles.sliderLabel}>Volume</Text>
              <Slider
                style={styles.slider}
                minimumValue={0}
                maximumValue={100}
                value={volume}
                onValueChange={setVolume}
                minimumTrackTintColor={Colors.primary}
                maximumTrackTintColor={Colors.surfaceContainerHigh}
                thumbTintColor={Colors.primary}
              />
              <View style={styles.sliderTicksRow}>
                <Text style={styles.sliderTick}>MIN</Text>
                <Text style={styles.sliderTick}>MAX</Text>
              </View>
            </View>
          </View>

          {/* Voice Speed Card */}
          <View style={styles.audioCard}>
            <View style={styles.audioCardHeader}>
              <View style={styles.iconBubble}>
                <MaterialIcons name="speed" size={20} color={Colors.primary} />
              </View>
              <Text style={styles.audioCardLabel}>SPEECH</Text>
            </View>
            <View>
              <Text style={styles.sliderLabel}>Voice Speed</Text>
              <Slider
                style={styles.slider}
                minimumValue={0.5}
                maximumValue={2.0}
                step={0.1}
                value={voiceSpeed}
                onValueChange={setVoiceSpeed}
                minimumTrackTintColor={Colors.primary}
                maximumTrackTintColor={Colors.surfaceContainerHigh}
                thumbTintColor={Colors.primary}
              />
              <View style={styles.sliderTicks3Row}>
                <Text style={styles.sliderTick}>0.5X</Text>
                <Text style={styles.sliderTick}>1.0X</Text>
                <Text style={styles.sliderTick}>2.0X</Text>
              </View>
            </View>
          </View>

          {/* Play Sample Button */}
          <TouchableOpacity
            activeOpacity={0.8}
            style={[styles.audioCard, styles.playSampleCard]}
            onPress={handlePlaySample}
            disabled={isPlayingSample}
          >
            <View style={styles.playSampleContent}>
              <View style={styles.playSampleIcon}>
                <MaterialIcons
                  name={isPlayingSample ? 'pause-circle-filled' : 'play-circle-filled'}
                  size={40}
                  color={Colors.primary}
                />
              </View>
              <View style={styles.playSampleText}>
                <Text style={styles.playSampleTitle}>
                  {isPlayingSample ? 'Playing Sample...' : 'Play Sample'}
                </Text>
                <Text style={styles.playSampleSubtitle}>
                  {voiceSpeed.toFixed(1)}x speed • {LANGUAGES[language]}
                </Text>
              </View>
            </View>
          </TouchableOpacity>
        </View>

        {/* Language & Image Quality */}
        <View style={styles.settingsCard}>
          {/* Language */}
          <View style={styles.settingsRow}>
            <View style={styles.settingsRowLeft}>
              <MaterialIcons name="language" size={22} color={Colors.onSurfaceVariant} />
              <View>
                <Text style={styles.settingsRowTitle}>Language</Text>
                <Text style={styles.settingsRowSubtitle}>Primary system voice</Text>
              </View>
            </View>
            <TouchableOpacity
              activeOpacity={0.7}
              style={styles.languagePill}
              onPress={() => setLanguage((prev) => (prev + 1) % LANGUAGES.length)}
            >
              <Text style={styles.languageText}>{LANGUAGES[language]}</Text>
            </TouchableOpacity>
          </View>

          {/* Image Quality */}
          <View style={styles.settingsRowVertical}>
            <View style={styles.settingsRowLeft}>
              <MaterialIcons name="high-quality" size={22} color={Colors.onSurfaceVariant} />
              <View>
                <Text style={styles.settingsRowTitle}>Image Quality</Text>
                <Text style={styles.settingsRowSubtitle}>Balance visual fidelity and battery life</Text>
              </View>
            </View>
            <View style={styles.segmentedContainer}>
              {QUALITY_OPTIONS.map((option) => (
                <TouchableOpacity
                  key={option}
                  activeOpacity={0.7}
                  onPress={() => setImageQuality(option)}
                  style={[
                    styles.segmentOption,
                    imageQuality === option && styles.segmentOptionActive,
                  ]}
                >
                  <Text
                    style={[
                      styles.segmentText,
                      imageQuality === option && styles.segmentTextActive,
                    ]}
                  >
                    {option}
                  </Text>
                </TouchableOpacity>
              ))}
            </View>
          </View>
        </View>

        {/* Device System Section */}
        <View style={styles.systemSection}>
          <Text style={styles.systemSectionTitle}>DEVICE SYSTEM</Text>

          {/* About Device */}
          <TouchableOpacity activeOpacity={0.8} style={styles.systemRow}>
            <View style={styles.systemRowLeft}>
              <MaterialIcons name="info" size={22} color={Colors.onSurfaceVariant} />
              <Text style={styles.systemRowText}>About Device</Text>
            </View>
            <View style={styles.systemRowRight}>
              <Text style={styles.systemRowMeta}>BONSAI v2.1</Text>
              <MaterialIcons
                name="chevron-right"
                size={20}
                color={Colors.outlineVariant}
              />
            </View>
          </TouchableOpacity>

          {/* Firmware Update */}
          <TouchableOpacity activeOpacity={0.8} style={styles.systemRow}>
            <View style={styles.systemRowLeft}>
              <MaterialIcons name="system-update" size={22} color={Colors.primary} />
              <Text style={styles.systemRowText}>Firmware Update</Text>
            </View>
            <View style={styles.systemRowRight}>
              <View style={styles.updateDot} />
              <Text style={styles.updateText}>UPDATE READY</Text>
              <MaterialIcons
                name="chevron-right"
                size={20}
                color={Colors.primary}
              />
            </View>
          </TouchableOpacity>
        </View>

        {/* Disconnect Button */}
        <TouchableOpacity activeOpacity={0.8} style={styles.disconnectButton}>
          <Text style={styles.disconnectText}>Disconnect Device</Text>
        </TouchableOpacity>
      </ScrollView>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: Colors.background,
  },
  scroll: {
    flex: 1,
  },
  scrollContent: {
    paddingHorizontal: 20,
    paddingBottom: 120,
  },
  headerSection: {
    marginBottom: 32,
    marginTop: 12,
  },
  pageTitle: {
    fontFamily: Fonts.headlineExtraBold,
    fontSize: 42,
    color: Colors.onBackground,
    letterSpacing: -1,
    marginBottom: 4,
  },
  pageSubtitle: {
    fontFamily: Fonts.bodyMedium,
    fontSize: 15,
    color: Colors.onSurfaceVariant,
  },
  audioGrid: {
    gap: 12,
    marginBottom: 16,
  },
  audioCard: {
    backgroundColor: Colors.surfaceContainerLowest,
    borderRadius: BorderRadius.default,
    padding: 24,
    gap: 24,
  },
  audioCardHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  iconBubble: {
    width: 40,
    height: 40,
    borderRadius: BorderRadius.full,
    backgroundColor: 'rgba(174, 141, 255, 0.15)',
    alignItems: 'center',
    justifyContent: 'center',
  },
  audioCardLabel: {
    fontFamily: Fonts.headlineBold,
    fontSize: 12,
    letterSpacing: 2,
    color: Colors.onSurfaceVariant,
  },
  sliderLabel: {
    fontFamily: Fonts.headlineBold,
    fontSize: 18,
    color: Colors.onSurface,
    marginBottom: 16,
  },
  slider: {
    width: '100%',
    height: 24,
  },
  sliderTicksRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    marginTop: 8,
  },
  sliderTicks3Row: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    marginTop: 8,
  },
  sliderTick: {
    fontFamily: Fonts.labelBold,
    fontSize: 11,
    color: Colors.outlineVariant,
  },
  settingsCard: {
    backgroundColor: Colors.surfaceContainerLowest,
    borderRadius: BorderRadius.default,
    padding: 24,
    gap: 32,
    marginBottom: 24,
  },
  settingsRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  settingsRowVertical: {
    gap: 16,
  },
  settingsRowLeft: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 16,
    flex: 1,
  },
  settingsRowTitle: {
    fontFamily: Fonts.headlineBold,
    fontSize: 15,
    color: Colors.onSurface,
  },
  settingsRowSubtitle: {
    fontFamily: Fonts.label,
    fontSize: 11,
    color: Colors.onSurfaceVariant,
    marginTop: 2,
  },
  languagePill: {
    backgroundColor: Colors.surfaceContainerLow,
    paddingHorizontal: 20,
    paddingVertical: 10,
    borderRadius: BorderRadius.full,
  },
  languageText: {
    fontFamily: Fonts.headlineBold,
    fontSize: 13,
    color: Colors.onSurface,
  },
  segmentedContainer: {
    flexDirection: 'row',
    backgroundColor: Colors.surfaceContainerLow,
    borderRadius: BorderRadius.full,
    padding: 4,
    gap: 4,
  },
  segmentOption: {
    flex: 1,
    paddingVertical: 10,
    borderRadius: BorderRadius.full,
    alignItems: 'center',
  },
  segmentOptionActive: {
    backgroundColor: Colors.surfaceContainerLowest,
    ...Shadows.card,
  },
  segmentText: {
    fontFamily: Fonts.headlineBold,
    fontSize: 12,
    color: Colors.onSurfaceVariant,
  },
  segmentTextActive: {
    color: Colors.primary,
  },
  systemSection: {
    gap: 12,
  },
  systemSectionTitle: {
    fontFamily: Fonts.headlineBold,
    fontSize: 12,
    letterSpacing: 4,
    color: Colors.outlineVariant,
    paddingHorizontal: 8,
    marginBottom: 4,
  },
  systemRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    backgroundColor: Colors.surfaceContainerLowest,
    padding: 20,
    borderRadius: BorderRadius.default,
  },
  systemRowLeft: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 16,
  },
  systemRowText: {
    fontFamily: Fonts.headlineBold,
    fontSize: 15,
    color: Colors.onSurface,
  },
  systemRowRight: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
  },
  systemRowMeta: {
    fontFamily: Fonts.labelBold,
    fontSize: 11,
    color: Colors.outlineVariant,
  },
  updateDot: {
    width: 8,
    height: 8,
    borderRadius: 4,
    backgroundColor: Colors.primary,
  },
  updateText: {
    fontFamily: Fonts.labelBold,
    fontSize: 11,
    color: Colors.primary,
  },
  disconnectButton: {
    marginTop: 32,
    paddingVertical: 16,
    borderRadius: BorderRadius.full,
    backgroundColor: 'rgba(180, 19, 64, 0.1)',
    alignItems: 'center',
  },
  disconnectText: {
    fontFamily: Fonts.headlineBold,
    fontSize: 16,
    color: Colors.error,
  },
  playSampleCard: {
    padding: 16,
  },
  playSampleContent: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 16,
  },
  playSampleIcon: {
    width: 56,
    height: 56,
    borderRadius: BorderRadius.full,
    backgroundColor: 'rgba(106, 55, 212, 0.1)',
    alignItems: 'center',
    justifyContent: 'center',
  },
  playSampleText: {
    flex: 1,
    gap: 2,
  },
  playSampleTitle: {
    fontFamily: Fonts.headlineBold,
    fontSize: 16,
    color: Colors.onSurface,
  },
  playSampleSubtitle: {
    fontFamily: Fonts.labelBold,
    fontSize: 12,
    color: Colors.onSurfaceVariant,
    letterSpacing: 0.5,
  },
});
