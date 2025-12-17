import { useState } from "react";
import ColorPicker from "react-pick-color";
import type { PanelProps } from "../types/PanelProps";

function ControlPanel({ characteristic, setStatus }: PanelProps) {
    const [color, setColor] = useState<string>("FFFFFF");

    async function sendColor() {
        if (!characteristic) {
            setStatus("Not connected");
            return;
        }

        let value = color.trim();
        if (!value.startsWith("#")) value = "#" + value;

        const encoder = new TextEncoder();
        const data = encoder.encode(value);
        console.log(value);

        try {
            await characteristic.writeValue(data);
            setStatus("Sent " + value);
        } catch (err: any) {
            console.error(err);
            setStatus("Write error: " + err.message);
        }
    }

    return (
        <div className="flex flex-col bg-red-100 m-5 p-5 rounded-sm items-center">
            <ColorPicker
                color={color}
                onChange={(newColor) => setColor(newColor.hex)}
            />

            <button
                className="bg-blue-200 px-12 py-1 mt-4 font-bold rounded-2xl"
                onClick={sendColor}
                disabled={!characteristic}
            >
                Send color
            </button>
        </div>
    );
}

export default ControlPanel;
