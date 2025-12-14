import { type Dispatch, type SetStateAction } from "react";

export type PanelProps = {
    characteristic: BluetoothRemoteGATTCharacteristic,
    setStatus: Dispatch<SetStateAction<string>>
}
