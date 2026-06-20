import React, { useEffect, useRef, useState } from 'react';
import {
  View,
  Text,
  StyleSheet,
  TouchableOpacity,
  Animated,
} from 'react-native';
import { MaterialIcons } from '@expo/vector-icons';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import Header from '../components/Header';
import { Colors, Fonts, BorderRadius, Shadows } from '../constants/theme';

type ConnectionState = 'searching' | 'connected' | 'disconnected';

function RadarAnimation() {
  const ring1 = useRef(new Animated.Value(0)).current;
  const ring2 = useRef(new Animated.Value(0)).current;
  const ring3 = useRef(new Animated.Value(0)).current;

  const createRingAnimation = (anim: Animated.Value, delay: number) =>
    Animated.loop(
      Animated.sequence([
        Animated.delay(delay),
        Animated.parallel([
          Animated.timing(anim, {
            toValue: 1,
            duration: 2000,
            useNativeDriver: true,
          }),
        ]),
        Animated.timing(anim, {
          toValue: 0,
          duration: 0,
          useNativeDriver: true,
        }),
      ])
    );

  useEffect(() => {
    createRingAnimation(ring1, 0).start();
    createRingAnimation(ring2, 600).start();
    createRingAnimation(ring3, 1200).start();
  }, []);

  const createRingStyle = (anim: Animated.Value) => ({
    transform: [
      {
        scale: anim.interpolate({
          inputRange: [0, 1],
          outputRange: [0.8, 2.5],
        }),
      },
    ],
    opacity: anim.interpolate({
      inputRange: [0, 1],
      outputRange: [0.8, 0],
    }),
  });

  return (
    <View style={radarStyles.container}>
      <Animated.View style={[radarStyles.ring, createRingStyle(ring1)]} />
      <Animated.View style={[radarStyles.ring, { backgroundColor: 'rgba(52, 211, 153, 0.1)' }, createRingStyle(ring2)]} />
      <Animated.View style={[radarStyles.ring, { backgroundColor: 'rgba(52, 211, 153, 0.05)' }, createRingStyle(ring3)]} />
      <View style={radarStyles.center}>
        <MaterialIcons name="bluetooth" size={48} color={Colors.primary} />
      </View>
    </View>
  );
}

const radarStyles = StyleSheet.create({
  container: {
    width: 128,
    height: 128,
    alignItems: 'center',
    justifyContent: 'center',
  },
  ring: {
    position: 'absolute',
    width: 96,
    height: 96,
    borderRadius: 48,
    backgroundColor: 'rgba(52, 211, 153, 0.2)',
  },
  center: {
    width: 128,
    height: 128,
    borderRadius: 64,
    backgroundColor: Colors.surfaceContainerLowest,
    alignItems: 'center',
    justifyContent: 'center',
    ...Shadows.card,
    zIndex: 10,
  },
});

export default function ConnectionScreen() {
  const insets = useSafeAreaInsets();
  const [connectionState, setConnectionState] = useState<ConnectionState>('searching');

  return (
    <View style={[styles.container, { paddingTop: insets.top }]}>
      <Header />
      <View style={styles.content}>
        {/* Radar Animation */}
        <RadarAnimation />

        {/* Connection Status */}
        <View style={styles.statusSection}>
          <Text style={styles.statusTitle}>
            {connectionState === 'searching' && 'Searching for Glasses...'}
            {connectionState === 'connected' && 'Glasses Connected'}
            {connectionState === 'disconnected' && 'Glasses Disconnected'}
          </Text>
          <Text style={styles.statusSubtitle}>
            {connectionState === 'searching' && 'Make sure pairing mode is active'}
            {connectionState === 'connected' && 'Ready to capture and process'}
            {connectionState === 'disconnected' && 'Tap to reconnect'}
          </Text>
        </View>

        {/* Connection Info Card */}
        <View style={styles.infoCard}>
          <View style={styles.infoRow}>
            <View style={styles.infoLabel}>
              <MaterialIcons name="hardware" size={18} color={Colors.onSurfaceVariant} />
              <Text style={styles.infoText}>Device</Text>
            </View>
            <Text style={styles.infoValue}>Bonsai Glasses v1</Text>
          </View>

          <View style={[styles.infoRow, styles.borderTop]}>
            <View style={styles.infoLabel}>
              <MaterialIcons name="battery-full" size={18} color={Colors.onSurfaceVariant} />
              <Text style={styles.infoText}>Battery</Text>
            </View>
            <Text style={styles.infoValue}>88%</Text>
          </View>

          <View style={[styles.infoRow, styles.borderTop]}>
            <View style={styles.infoLabel}>
              <MaterialIcons name="signal-cellular-alt" size={18} color={Colors.onSurfaceVariant} />
              <Text style={styles.infoText}>Signal</Text>
            </View>
            <Text style={styles.infoValue}>Strong</Text>
          </View>
        </View>

        {/* Action Button */}
        <TouchableOpacity
          activeOpacity={0.8}
          style={styles.actionButton}
          onPress={() => 
            setConnectionState(connectionState === 'connected' ? 'disconnected' : 'connected')
          }
        >
          <MaterialIcons
            name={connectionState === 'connected' ? 'bluetooth-connected' : 'bluetooth'}
            size={24}
            color={Colors.white}
          />
          <Text style={styles.actionButtonText}>
            {connectionState === 'connected' ? 'Disconnect' : 'Connect'}
          </Text>
        </TouchableOpacity>
      </View>
    </View>
  );
}


const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: Colors.background,
  },
  content: {
    flex: 1,
    paddingHorizontal: 20,
    justifyContent: 'center',
    alignItems: 'center',
    gap: 40,
  },
  statusSection: {
    alignItems: 'center',
    gap: 8,
  },
  statusTitle: {
    fontFamily: Fonts.headlineBold,
    fontSize: 24,
    color: Colors.onBackground,
    letterSpacing: -0.5,
  },
  statusSubtitle: {
    fontFamily: Fonts.bodyMedium,
    fontSize: 13,
    color: Colors.onSurfaceVariant,
    textAlign: 'center',
  },
  infoCard: {
    backgroundColor: Colors.surfaceContainerLowest,
    borderRadius: BorderRadius.default,
    overflow: 'hidden',
    width: '100%',
    maxWidth: 320,
  },
  infoRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    paddingHorizontal: 20,
    paddingVertical: 16,
  },
  borderTop: {
    borderTopWidth: 1,
    borderTopColor: Colors.surfaceContainerHigh,
  },
  infoLabel: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
  },
  infoText: {
    fontFamily: Fonts.labelBold,
    fontSize: 12,
    color: Colors.onSurfaceVariant,
    letterSpacing: 1,
  },
  infoValue: {
    fontFamily: Fonts.headlineBold,
    fontSize: 14,
    color: Colors.onBackground,
  },
  actionButton: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    gap: 12,
    backgroundColor: Colors.primary,
    paddingHorizontal: 32,
    paddingVertical: 14,
    borderRadius: BorderRadius.full,
    ...Shadows.card,
  },
  actionButtonText: {
    fontFamily: Fonts.headlineBold,
    fontSize: 16,
    color: Colors.white,
  },
});
