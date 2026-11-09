// The one settings document (handoff section 4). Everything the app shows is
// derived from this plus live state. Designed so Phase 2 can serialize the
// per-petal part into a hub SysEx message.

import { KEYS_PER_PETAL, noteFor } from "../midi/hubTable";
import { GM_NAMES } from "../midi/gmInstruments";

export type NoteNumber = number; // MIDI 0..127; C4 = 60 (middle C)
export type ExpressionMode = "swell" | "fixed";
export type RoleOverride = "auto" | "top" | "inner" | "bass" | "solo";
export type Role = "top" | "inner" | "bass" | "solo";

export interface Instrument {
  bank: number;
  program: number;
  name: string;
}

export interface PetalLayout {
  rotation: 0 | 1 | 2 | 3 | 4 | 5;
  mirrored: boolean;
}

export interface PetalConfig {
  id: number; // 1..30 (UI shows 1..6)
  name: string;
  instrument: Instrument;
  keys: (NoteNumber | null)[]; // length 6, index k = physical key C..A; null = silent
  expressionMode: ExpressionMode;
  volume: number; // 0..1
  pan: number; // -1..1
  layout: PetalLayout;
  roleOverride: RoleOverride;
}

export interface TuningConfig {
  mode: "equal" | "ensemble";
  scale: number; // multiplies all model offsets
  roleOffsets: { top: number; inner: number; bass: number; solo: number }; // cents
  stepDown: { enabled: boolean; cents: number; maxSemitones: number; windowSec: number };
  leapDown: { enabled: boolean; cents: number };
  longNote: { enabled: boolean; cents: number; startSec: number; fullSec: number };
  drift: { enabled: boolean; sigmaCents: number; tauSec: number; initialCents: number; clampCents: number };
}

export interface FeelConfig {
  velocityFloor: number;
  velocityCurve: number;
  pressureFloor: number;
  digitalKeyVelocity: number;
}

export interface MidiOutConfig {
  enabled: boolean;
  portId: string | null;
  muteBuiltIn: boolean;
}

export interface AppConfig {
  version: 1;
  petals: Record<number, PetalConfig>;
  tuning: TuningConfig;
  feel: FeelConfig;
  midiOut: MidiOutConfig;
  presetName: string;
}

/** Default expression mode by GM family (handoff 6.5). */
export function defaultExpressionMode(program: number): ExpressionMode {
  const swell =
    (program >= 16 && program <= 23) || // organ
    (program >= 40 && program <= 55) || // strings / ensemble / choir
    (program >= 56 && program <= 63) || // brass
    (program >= 64 && program <= 71) || // reed
    (program >= 72 && program <= 79) || // pipe
    (program >= 80 && program <= 95); // synth lead / pad
  return swell ? "swell" : "fixed";
}

/** New-petal defaults: Grand Piano, the hub's own notes, layout rotation 0. */
export function defaultPetalConfig(id: number): PetalConfig {
  const program = 0;
  return {
    id,
    name: `Petal ${id}`,
    instrument: { bank: 0, program, name: GM_NAMES[program] },
    keys: Array.from({ length: KEYS_PER_PETAL }, (_, k) => noteFor(id, k)),
    expressionMode: defaultExpressionMode(program),
    volume: 0.8,
    pan: 0,
    layout: { rotation: 0, mirrored: false },
    roleOverride: "auto",
  };
}

export function defaultTuningConfig(): TuningConfig {
  return {
    mode: "ensemble",
    scale: 1.0,
    roleOffsets: { top: 10, inner: -5, bass: 0, solo: 0 },
    stepDown: { enabled: true, cents: 3, maxSemitones: 2, windowSec: 5 },
    leapDown: { enabled: false, cents: -2.5 },
    longNote: { enabled: true, cents: -3, startSec: 1.0, fullSec: 2.0 },
    drift: { enabled: true, sigmaCents: 1.5, tauSec: 60, initialCents: 1.5, clampCents: 5 },
  };
}

export function defaultFeelConfig(): FeelConfig {
  // Light-touch defaults (keycaps): soft presses start at ~45% volume and the
  // curve lifts light strikes. The MIT version used 20 / 1.0 / 20.
  return { velocityFloor: 57, velocityCurve: 0.65, pressureFloor: 50, digitalKeyVelocity: 100 };
}

export function defaultAppConfig(): AppConfig {
  const petals: Record<number, PetalConfig> = {};
  for (let id = 1; id <= 6; id++) petals[id] = defaultPetalConfig(id);
  return {
    version: 1,
    petals,
    tuning: defaultTuningConfig(),
    feel: defaultFeelConfig(),
    midiOut: { enabled: false, portId: null, muteBuiltIn: false },
    presetName: "Default",
  };
}
