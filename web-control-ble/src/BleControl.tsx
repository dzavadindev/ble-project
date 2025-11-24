import React, { useState } from "react";
import ColorPicker from "react-pick-color";

const SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
const CHAR_COLOR_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c3319101";

function App() {
    const [color, setColor] = useState("#ffffff");
  const [device, setDevice] = useState<BluetoothDevice | null>(null);
  const [characteristic, setCharacteristic] =
    useState<BluetoothRemoteGATTCharacteristic | null>(null);
  const [status, setStatus] = useState("Not connected");
  const [hexColor, setHexColor] = useState("#FF0000");

  async function connect() {
    try {
      setStatus("Requesting device…");

      const dev = await navigator.bluetooth.requestDevice({
        // Start simple: show all devices, but ask for our service
        acceptAllDevices: true,
        optionalServices: [SERVICE_UUID],
        // later you can switch to:
        // filters: [{ namePrefix: "StairLight" }],
      });

      setStatus("Connecting…");
      const server = await dev.gatt!.connect();

      setStatus("Getting service…");
      const service = await server.getPrimaryService(SERVICE_UUID);

      setStatus("Getting characteristic…");
      const char = await service.getCharacteristic(CHAR_COLOR_UUID);

      setDevice(dev);
      setCharacteristic(char);
      setStatus(`Connected to ${dev.name || dev.id}`);

      // handle disconnect
      dev.addEventListener("gattserverdisconnected", () => {
        setStatus("Disconnected");
        setDevice(null);
        setCharacteristic(null);
      });
    } catch (err: any) {
      console.error(err);
      setStatus("Error: " + err.message);
    }
  }

  async function sendColor() {
    if (!characteristic) {
      setStatus("Not connected");
      return;
    }

    // Ensure it starts with #
    let value = color.trim();
    if (!value.startsWith("#")) value = "#" + value;

    // send as UTF-8 text (what your ESP32 expects)
    const encoder = new TextEncoder();
    const data = encoder.encode(value);

    try {
      await characteristic.writeValue(data);
      setStatus("Sent " + value);
    } catch (err: any) {
      console.error(err);
      setStatus("Write error: " + err.message);
    }
  }

  return (
    <div style={{ fontFamily: "sans-serif", padding: "1rem", maxWidth: 400 }}>
      <h1>StairLight BLE</h1>
      <p>Status: {status}</p>

      <button onClick={connect} disabled={!!device}>
        {device ? "Connected" : "Connect to StairLight"}
      </button>
      <div>
        <ColorPicker
        color={color}
        onChange={(newColor) => setColor(newColor.hex)}
        />
    </div>

      <button
        style={{ marginTop: "0.5rem" }}
        onClick={sendColor}
        disabled={!characteristic}
      >
        Send color
      </button>
    </div>
  );
}

export default App;
