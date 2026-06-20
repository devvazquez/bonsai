import React from 'react';
import { View, Text, Image, StyleSheet, TouchableOpacity } from 'react-native';
import { MaterialIcons } from '@expo/vector-icons';
import { Colors, Fonts, BorderRadius } from '../constants/theme';

const AVATAR_URL =
  'https://lh3.googleusercontent.com/aida-public/AB6AXuBIifqDHWxvaUTjvZ_MLwJ_QhZfs7MTgpTomr4FQO812AeM43YbgvxbJMvlUF9nryIqkgGpGPwIlaRI77Ut7SWFMc9eWWlAAd8laSCUxMfzlUIejWWmxxbYtrFtfQvhPaHsj5i5JIU6knwTlTC_IFncoRB0I4-aTSW0_ZpL4Pb8wqtSjlhvCM3i7TtLvPl9UJMgoBihjA1-KoStdRbumpJLUswpAWy1mYp4ONxK0UDmTKjmAltUpyggDk9KwzXXLIN9J7a-SD8V2Wg';

export default function Header() {
  return (
    <View style={styles.container}>
      <View style={styles.left}>
        <MaterialIcons
          name="settings-input-antenna"
          size={22}
          color={Colors.primary}
        />
        <Text style={styles.title}>BONSAI</Text>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: 20,
    height: 56,
    backgroundColor: 'rgba(255, 255, 255, 0.8)',
  },
  left: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 10,
  },
  title: {
    fontSize: 18,
    fontFamily: Fonts.headlineExtraBold,
    letterSpacing: 4,
    color: Colors.slate900,
  },
  avatarContainer: {
    width: 38,
    height: 38,
    borderRadius: BorderRadius.full,
    overflow: 'hidden',
    borderWidth: 2,
    borderColor: Colors.primaryContainer,
  },
  avatar: {
    width: '100%',
    height: '100%',
  },
});
