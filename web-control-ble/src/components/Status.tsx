import { useState, useEffect } from "react";
import type { PanelProps } from "../types/PanelProps";

function StatusPanel({ characteristic, setStatus }: PanelProps) {
    const [range, setRange] = useState<[number, number]>([0, 0]);

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
        <div className="flex flex-col bg-red-200 m-5 p-5 rounded-sm text-center">
            <span>
                Currectly active range: {range[0]}, {range[1]}
            </span>
        </div>
    );
}

export default StatusPanel;
