// Touch controls: how loud light presses sound. The petals report strike
// velocity in a narrow 90–127 band and pressure 1–127; these sliders set how
// that maps to loudness (feel.velocityFloor / velocityCurve / pressureFloor,
// used in engine/voiceEngine.ts). Whether a press registers at all is set in
// the petal firmware (firmware/petal, sensitivity level).

import { configStore, useConfig } from "../state/store";
import { defaultFeelConfig } from "../state/config";

export function TouchControls() {
  const f = useConfig().feel;
  const set = (patch: Partial<typeof f>) => configStore.update({ feel: { ...f, ...patch } });
  // Slider shows "light-touch boost": 0 = linear, 100 = strongest boost.
  const boost = Math.round(((1 - f.velocityCurve) / 0.7) * 100);

  return (
    <section className="panel tuning-controls touch-controls">
      <strong className="touch-title">Touch</strong>
      <label className="slider scale-slider">
        Softest press volume {Math.round((f.velocityFloor / 127) * 100)}%
        <input
          type="range"
          min={1}
          max={110}
          step={1}
          value={f.velocityFloor}
          onChange={(e) => set({ velocityFloor: Number(e.target.value) })}
        />
      </label>
      <label className="slider scale-slider">
        Light-touch boost {Math.max(0, boost)}
        <input
          type="range"
          min={0}
          max={100}
          step={5}
          value={Math.max(0, boost)}
          onChange={(e) => set({ velocityCurve: 1 - (Number(e.target.value) / 100) * 0.7 })}
        />
      </label>
      <label className="slider scale-slider">
        Softest held level {Math.round((f.pressureFloor / 127) * 100)}%
        <input
          type="range"
          min={1}
          max={110}
          step={1}
          value={f.pressureFloor}
          onChange={(e) => set({ pressureFloor: Number(e.target.value) })}
        />
      </label>
      <button onClick={() => set(defaultFeelConfig())}>Reset</button>
    </section>
  );
}
