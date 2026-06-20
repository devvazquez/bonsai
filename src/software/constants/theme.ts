/**
 * Bonsai Design System
 * Extracted from Stitch project - "The Digital Curator"
 *
 * Design Principles:
 * - No visible borders — use tonal background shifts
 * - Glassmorphism for floating elements
 * - Pill-shaped buttons (full border radius)
 * - Ambient shadows (diffused, tinted, never harsh)
 * - Dual font: Manrope (headlines), Inter (body/labels)
 */

export const Colors = {
  // Primary
  primary: '#34d399',
  primaryContainer: '#6ee7b7',
  primaryDim: '#10b981',
  primaryFixed: '#6ee7b7',
  primaryFixedDim: '#34d399',
  onPrimary: '#f8f0ff',
  onPrimaryContainer: '#2b006e',
  onPrimaryFixed: '#000000',
  onPrimaryFixedVariant: '#370086',

  // Secondary
  secondary: '#545b6d',
  secondaryContainer: '#d9dff5',
  secondaryDim: '#494f61',
  secondaryFixed: '#d9dff5',
  secondaryFixedDim: '#cbd1e6',
  onSecondary: '#f0f2ff',
  onSecondaryContainer: '#495061',
  onSecondaryFixed: '#373d4e',
  onSecondaryFixedVariant: '#535a6b',

  // Tertiary
  tertiary: '#9d365b',
  tertiaryContainer: '#ff8eb0',
  tertiaryDim: '#8e294f',
  tertiaryFixed: '#ff8eb0',
  tertiaryFixedDim: '#f67ca3',
  onTertiary: '#ffeff1',
  onTertiaryContainer: '#640430',
  onTertiaryFixed: '#380018',
  onTertiaryFixedVariant: '#701039',

  // Error
  error: '#b41340',
  errorContainer: '#f74b6d',
  errorDim: '#a70138',
  onError: '#ffefef',
  onErrorContainer: '#510017',

  // Surface
  surface: '#f5f6f7',
  surfaceBright: '#f5f6f7',
  surfaceDim: '#d1d5d7',
  surfaceVariant: '#dadddf',
  surfaceTint: '#6a37d4',
  surfaceContainer: '#e6e8ea',
  surfaceContainerHigh: '#e0e3e4',
  surfaceContainerHighest: '#dadddf',
  surfaceContainerLow: '#eff1f2',
  surfaceContainerLowest: '#ffffff',

  // Background
  background: '#f5f6f7',
  onBackground: '#2c2f30',

  // On Surface
  onSurface: '#2c2f30',
  onSurfaceVariant: '#595c5d',

  // Outline
  outline: '#757778',
  outlineVariant: '#abadae',

  // Inverse
  inverseSurface: '#0c0f10',
  inverseOnSurface: '#9b9d9e',
  inversePrimary: '#a078ff',

  // Utility
  white: '#ffffff',
  black: '#000000',
  transparent: 'transparent',

  // Semantic
  success: '#10b981',
  slate400: '#94a3b8',
  slate500: '#64748b',
  slate900: '#0f172a',
} as const;

export const Fonts = {
  headline: 'Manrope_400Regular',
  headlineBold: 'Manrope_700Bold',
  headlineExtraBold: 'Manrope_800ExtraBold',
  body: 'Inter_400Regular',
  bodyMedium: 'Inter_500Medium',
  bodySemiBold: 'Inter_600SemiBold',
  bodyBold: 'Inter_700Bold',
  label: 'Inter_400Regular',
  labelMedium: 'Inter_500Medium',
  labelSemiBold: 'Inter_600SemiBold',
  labelBold: 'Inter_700Bold',
} as const;

export const BorderRadius = {
  sm: 8,
  default: 16,
  lg: 24,
  xl: 32,
  '2xl': 48,
  full: 9999,
} as const;

export const Spacing = {
  xs: 4,
  sm: 8,
  md: 12,
  lg: 16,
  xl: 20,
  '2xl': 24,
  '3xl': 32,
  '4xl': 40,
  '5xl': 48,
  '6xl': 64,
} as const;

export const Shadows = {
  ambient: {
    shadowColor: '#2c2f30',
    shadowOffset: { width: 0, height: 8 },
    shadowOpacity: 0.06,
    shadowRadius: 24,
    elevation: 4,
  },
  card: {
    shadowColor: '#2c2f30',
    shadowOffset: { width: 0, height: 4 },
    shadowOpacity: 0.04,
    shadowRadius: 16,
    elevation: 2,
  },
  floating: {
    shadowColor: '#6a37d4',
    shadowOffset: { width: 0, height: 20 },
    shadowOpacity: 0.3,
    shadowRadius: 50,
    elevation: 12,
  },
  navBar: {
    shadowColor: '#000000',
    shadowOffset: { width: 0, height: 20 },
    shadowOpacity: 0.1,
    shadowRadius: 50,
    elevation: 8,
  },
} as const;
