import React from 'react';
import { View, TouchableOpacity, StyleSheet } from 'react-native';
import { BlurView } from 'expo-blur';
import { MaterialIcons } from '@expo/vector-icons';
import type { BottomTabBarProps } from '@react-navigation/bottom-tabs';
import { Colors } from '../constants/theme';

type TabConfig = {
  icon: keyof typeof MaterialIcons.glyphMap;
};

const TABS: Record<string, TabConfig> = {
  index: { icon: 'sensors' },
  devices: { icon: 'router' },
  settings: { icon: 'tune' },
};

export default function TabBar({ state, navigation }: BottomTabBarProps) {
  return (
    <View style={styles.wrapper}>
      <BlurView intensity={80} tint="light" style={styles.blurContainer}>
        <View style={styles.container}>
          {state.routes.map((route, index) => {
            const isActive = state.index === index;
            const tab = TABS[route.name] || { icon: 'circle' };

            return (
              <TouchableOpacity
                key={route.key}
                activeOpacity={0.6}
                onPress={() => navigation.navigate(route.name)}
                style={[styles.tab, isActive && styles.activeTab]}
              >
                <MaterialIcons
                  name={tab.icon}
                  size={28}
                  color={isActive ? Colors.primary : Colors.outlineVariant}
                />
              </TouchableOpacity>
            );
          })}
        </View>
      </BlurView>
    </View>
  );
}

const styles = StyleSheet.create({
  wrapper: {
    position: 'absolute',
    bottom: 24,
    left: 24,
    right: 24,
    zIndex: 50,
  },
  blurContainer: {
    borderRadius: 24,
    overflow: 'hidden',
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 10 },
    shadowOpacity: 0.1,
    shadowRadius: 20,
    elevation: 8,
    borderWidth: 1,
    borderColor: 'rgba(255, 255, 255, 0.2)',
  },
  container: {
    flexDirection: 'row',
    justifyContent: 'space-around',
    alignItems: 'center',
    height: 64,
    backgroundColor: 'rgba(255, 255, 255, 0.8)',
  },
  tab: {
    flex: 1,
    alignItems: 'center',
    justifyContent: 'center',
    height: '100%',
  },
  activeTab: {
    transform: [{ scale: 1.25 }],
  },
});
