import { useState, useEffect } from "react";
import type { PanelProps } from "../types/PanelProps";

type LedStripProps = {
    size: number,
    start: number,
    end: number
}

function LedStrip({ size, start, end }: LedStripProps) {
    // Convert LED indices -> percentages so we can draw the active range as a bar
    const rangeWidth = ((end - start) / size) * 100;
    const rangeLeft = (start / size) * 100;

    return (
        <div className="strip">
            <div
                className="range"
                style={{
                    width: `${rangeWidth}%`,
                    left: `${rangeLeft}%`,
                }}
            />
        </div>
    );
}

function StatusPanel({ characteristic, setStatus }: PanelProps) {
    // [start, end] LED indices received from BLE
    const [range, setRange] = useState<[number, number]>([0, 0]);

    useEffect(() => {
        if (!characteristic) return;

        // Listen for notifications/updates from the BLE characteristic
        characteristic.addEventListener("characteristicvaluechanged", (event: any) => {
            const char = event.target as BluetoothRemoteGATTCharacteristic;
            const dv = char.value!; // DataView with the payload bytes

            // Payload: byte0=start, byte1=end
            const start = dv.getUint8(0, true);
            const end = dv.getUint8(1, true);

            setStatus("Received new range status");
            setRange([start, end]);
        });
    }, [characteristic])

    return (
        <div className="flex flex-col bg-red-100 m-5 p-5 rounded-sm text-center">
            <span>
                Currectly active range: {range[0]}, {range[1]}
            </span>
            <LedStrip
                size={150}          // total LEDs on the strip
                start={range[0]}    // active range start
                end={range[1]}      // active range end
            />
        </div>
    );
}

export default StatusPanel;
