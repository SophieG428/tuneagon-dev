// Config store: holds the AppConfig, persists to localStorage (debounced 300 ms),
// and exposes a React hook. Updates are immutable so useSyncExternalStore works.

import { useSyncExternalStore } from "react";
import {
  type AppConfig,
  type PetalConfig,
  defaultAppConfig,
  defaultPetalConfig,
} from "./config";

// Separate key from the MIT-submitted site (same github.io origin), so changes
// here never touch that site's saved settings. On first load, petal setups and
// tuning are copied (read-only) from the old key; feel uses the new defaults.
const STORAGE_KEY = "tuneagon-dev.config.v1";
const LEGACY_KEY = "tunagon.config.v1";

class ConfigStore {
  private config: AppConfig;
  private listeners = new Set<() => void>();
  private saveTimer: ReturnType<typeof setTimeout> | undefined;

  constructor() {
    this.config = this.load();
  }

  private load(): AppConfig {
    try {
      let raw = localStorage.getItem(STORAGE_KEY);
      let fromLegacy = false;
      if (!raw) { raw = localStorage.getItem(LEGACY_KEY); fromLegacy = raw != null; }
      if (raw) {
        const parsed = JSON.parse(raw) as Partial<AppConfig>;
        if (fromLegacy) delete parsed.feel;
        // Shallow-merge onto defaults for forward compatibility.
        const base = defaultAppConfig();
        return {
          ...base,
          ...parsed,
          petals: { ...base.petals, ...(parsed.petals ?? {}) },
          tuning: { ...base.tuning, ...(parsed.tuning ?? {}) },
          feel: { ...base.feel, ...(parsed.feel ?? {}) },
          midiOut: { ...base.midiOut, ...(parsed.midiOut ?? {}) },
        };
      }
    } catch (e) {
      console.warn("Tuneagon: could not load saved config, using defaults.", e);
    }
    return defaultAppConfig();
  }

  get(): AppConfig {
    return this.config;
  }

  subscribe = (l: () => void): (() => void) => {
    this.listeners.add(l);
    return () => this.listeners.delete(l);
  };

  getSnapshot = (): AppConfig => this.config;

  private emit() {
    for (const l of this.listeners) l();
    this.scheduleSave();
  }

  private scheduleSave() {
    clearTimeout(this.saveTimer);
    this.saveTimer = setTimeout(() => {
      try {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(this.config));
      } catch (e) {
        console.warn("Tuneagon: could not save config.", e);
      }
    }, 300);
  }

  /** Current config for a petal, or defaults if not yet created (no write). */
  getPetal(id: number): PetalConfig {
    return this.config.petals[id] ?? defaultPetalConfig(id);
  }

  /** Create a petal's config on first sight if missing. Call from events, not render. */
  ensurePetal(id: number): PetalConfig {
    if (!this.config.petals[id]) {
      this.config = {
        ...this.config,
        petals: { ...this.config.petals, [id]: defaultPetalConfig(id) },
      };
      this.emit();
    }
    return this.config.petals[id];
  }

  updatePetal(id: number, patch: Partial<PetalConfig>) {
    const next = { ...this.getPetal(id), ...patch };
    this.config = { ...this.config, petals: { ...this.config.petals, [id]: next } };
    this.emit();
  }

  update(patch: Partial<AppConfig>) {
    this.config = { ...this.config, ...patch };
    this.emit();
  }

  replace(cfg: AppConfig) {
    this.config = cfg;
    this.emit();
  }

  exportJSON(): string {
    return JSON.stringify(this.config, null, 2);
  }

  importJSON(json: string) {
    const parsed = JSON.parse(json) as Partial<AppConfig>;
    this.replace({ ...defaultAppConfig(), ...parsed });
  }
}

export const configStore = new ConfigStore();

export function useConfig(): AppConfig {
  return useSyncExternalStore(configStore.subscribe, configStore.getSnapshot);
}
