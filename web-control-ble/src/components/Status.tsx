import { useState, useEffect } from "react";
import type { PanelProps } from "../types/PanelProps";

type LedStripProps = {
    size: number,
    start: number,
    end: number
}

function LedStrip({ size, start, end }: LedStripProps) {
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
    const [range, setRange] = useState<[number, number]>([0, 0]);
    const [mock_reading, setMockReading] = useState<number>(0);

    useEffect(() => {
        if (!characteristic) return;
        characteristic.addEventListener("characteristicvaluechanged", (event: any) => {
            const char = event.target as BluetoothRemoteGATTCharacteristic;
            const dv = char.value!;

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
            <div>
                <input
                    type="range"
                    min={10}
                    max={245}
                    value={mock_reading}
                    onChange={e => setMockReading(parseInt(e.target.value))}
                />
            </div>
            <LedStrip
                size={255}
                start={mock_reading - 10}
                end={mock_reading + 10}
            />
        </div>
    );
}

export default StatusPanel;
