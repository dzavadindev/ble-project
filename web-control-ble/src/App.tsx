import { useState } from "react";

import Control from "./components/Control";
import Status from "./components/Status";

const CONTROL_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
const CHAR_COLOR_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c3319101";

// 👇 ADD THIS (replace with your real brightness characteristic UUID)
const CHAR_BRIGHTNESS_UUID = "41db7a25-59f1-405e-b349-13afcfb5c806";

const STATUS_SERVICE_UUID = "71e1ebf9-7833-49a0-9fb5-580a753153de";
const CHAR_RANGE_UUID = "d6d17c21-5d8c-4942-83c2-6fdbd46a2989";

function App() {
  const [device, setDevice] = useState<BluetoothDevice | null>(null);
  const [status, setStatus] = useState("Not connected");

  const [colorChar, setColorChar] =
    useState<BluetoothRemoteGATTCharacteristic | null>(null);
  const [rangeChar, setRangeChar] =
    useState<BluetoothRemoteGATTCharacteristic | null>(null);

  // 👇 ADD THIS
  const [brightnessChar, setBrightnessChar] =
    useState<BluetoothRemoteGATTCharacteristic | null>(null);

  async function connect() {
    try {
      setStatus("Requesting device…");

      const dev = await navigator.bluetooth.requestDevice({
        acceptAllDevices: true,
        optionalServices: [CONTROL_SERVICE_UUID, STATUS_SERVICE_UUID],
      });

      setStatus("Connecting…");
      const server = await dev.gatt!.connect();
      setStatus("Connected");

      const controlService = await server.getPrimaryService(CONTROL_SERVICE_UUID);

      const colorChar = await controlService.getCharacteristic(CHAR_COLOR_UUID);

      // 👇 GET BRIGHTNESS CHAR
      const brightnessChar = await controlService.getCharacteristic(
        CHAR_BRIGHTNESS_UUID
      );

      setStatus("Control service up");

      const statusService = await server.getPrimaryService(STATUS_SERVICE_UUID);
      const rangeChar = await statusService.getCharacteristic(CHAR_RANGE_UUID);
      setStatus("Status service up");

      setDevice(dev);
      setColorChar(colorChar);
      setBrightnessChar(brightnessChar); // 👈 ADD
      setRangeChar(rangeChar);

      setStatus(`Connected to ${dev.name || dev.id}`);

      await rangeChar.startNotifications();

      dev.addEventListener("gattserverdisconnected", () => {
        setStatus("Disconnected");
        setDevice(null);
        setRangeChar(null);
        setColorChar(null);
        setBrightnessChar(null); // 👈 ADD
      });
    } catch (err: any) {
      console.error(err);
      setStatus("Error: " + err.message);
    }
  }

  return (
    <>
      <div className="flex flex-col bg-red-100 m-5 p-5 rounded-sm">
        <h1 className="font-bold text-4xl text-center w-full">StairLight BLE</h1>

        <div className="flex flex-row justify-center align-middle gap-6 my-4">
          <p className="font-light italic text-center align-middle">
            Status: {status}
          </p>

          <button
            className={`${
              device ? "bg-green-500 text-green-950" : "bg-blue-200"
            } px-3 py-1 rounded-2xl`}
            onClick={connect}
            disabled={!!device}
          >
            {device ? "Connected" : "Connect to StairLight"}
          </button>
        </div>
      </div>

      <Control
        characteristic={colorChar}
        brightnessCharacteristic={brightnessChar}  // 👈 ADD
        setStatus={setStatus}
      />

      <Status characteristic={rangeChar} setStatus={setStatus} />
    </>
  );
}

export default App;
