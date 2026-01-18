import { useEffect, useRef, useState } from "react";
import ColorPicker from "react-pick-color";
import type { PanelProps } from "../types/PanelProps";

function ControlPanel({ characteristic, brightnessCharacteristic, setStatus }: PanelProps) {
  const [color, setColor] = useState<string>("FFFFFF");
  const [brightness, setBrightness] = useState<number>(80);

  // read initial brightness from device once connected
  useEffect(() => {
    let cancelled = false;

    async function readInitialBrightness() {
      if (!brightnessCharacteristic) return;
      try {
        const dv = await brightnessCharacteristic.readValue();
        const br = dv.getUint8(0);
        if (!cancelled) setBrightness(br);
      } catch {

      }
    }

    readInitialBrightness();
    return () => {
      cancelled = true;
    };
  }, [brightnessCharacteristic]);

  async function sendColor() {
    if (!characteristic) {
      setStatus("Not connected");
      return;
    }

    let value = color.trim();
    if (!value.startsWith("#")) value = "#" + value;

    const data = new TextEncoder().encode(value);

    try {
      await characteristic.writeValue(data);
      setStatus("Sent " + value);
    } catch (err: any) {
      console.error(err);
      setStatus("Write error (color): " + err.message);
    }
  }

  // debounce brightness writes so the slider doesn't spam BLE
  const brTimer = useRef<number | null>(null);

  function queueBrightnessWrite(next: number) {
    setBrightness(next);

    if (!brightnessCharacteristic) {
      setStatus("Not connected (brightness)");
      return;
    }

    if (brTimer.current) window.clearTimeout(brTimer.current);
    brTimer.current = window.setTimeout(async () => {
      try {
        const v = Math.max(0, Math.min(255, next | 0));
        await brightnessCharacteristic.writeValue(new Uint8Array([v])); // <-- 1 byte
        setStatus("Brightness set to " + v);
      } catch (err: any) {
        console.error(err);
        setStatus("Write error (brightness): " + err.message);
      }
    }, 40);
  }

  return (
    <div className="flex flex-col bg-red-100 m-5 p-5 rounded-sm items-center gap-4">
      <ColorPicker
        color={color}
        onChange={(newColor) => setColor(newColor.hex)}
      />

      <button
        className="bg-blue-200 px-12 py-1 font-bold rounded-2xl"
        onClick={sendColor}
        disabled={!characteristic}
      >
        Send color
      </button>

      {/* Brightness */}
      <div className="w-full max-w-md bg-white/60 p-4 rounded-xl">
        <div className="flex items-center justify-between mb-2">
          <span className="font-semibold">Brightness</span>
          <span className="font-mono">{brightness}</span>
        </div>

        <input
          className="w-full"
          type="range"
          min={0}
          max={255}
          value={brightness}
          disabled={!brightnessCharacteristic}
          onChange={(e) => queueBrightnessWrite(Number(e.target.value))}
        />

        {!brightnessCharacteristic && (
          <div className="text-sm italic opacity-70 mt-2">
            Brightness characteristic not connected.
          </div>
        )}
      </div>
    </div>
  );
}

export default ControlPanel;
