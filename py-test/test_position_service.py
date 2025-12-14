import asyncio
from bleak import BleakScanner, BleakClient

SERVICE_UUID = "beb5483e-36e1-4688-b7f5-ea07361b2600"  # POSITION_SERVICE_UUID
CHAR_UUID = "beb5483e-36e1-4688-b7f5-ea07361b2601"  # CHAR_POSITION_UUID

TARGET_NAME = "StairLight"


async def find_stairlight():
    print("Scanning for BLE devices...")
    devices = await BleakScanner.discover(timeout=5.0)

    if not devices:
        print("No BLE devices found at all.")
        return None

    print("\nDevices found:")
    for d in devices:
        print(f"- name={d.name!r}  addr={d.address}")

    for d in devices:
        if d.name == TARGET_NAME:
            print(f"\nFound StairLight by name: {d.name} [{d.address}]")
            return d.address

    print(f"\nNo device with name {TARGET_NAME!r} found.")
    return None


async def main():
    address = await find_stairlight()
    if not address:
        return

    print(f"\nConnecting to {address} ...")
    async with BleakClient(address) as client:
        connected = client.is_connected
        print("Connected:", connected)
        if not connected:
            return

        # Print all services & characteristics so we can verify UUIDs
        print("\nServices on device:")
        for service in client.services:
            print(f"  [Service] {service.uuid}")
            for char in service.characteristics:
                print(f"    [Char] {char.uuid}  props={char.properties}")

        print("\nSending test XY positions to positionService characteristic...")
        for i in range(10):
            x_mm = 100 + i * 50   # dummy positions
            y_mm = 500 + i * 100

            # Pack as 4 bytes little-endian (int16, int16)
            payload = (
                int(x_mm).to_bytes(2, byteorder="little", signed=True) +
                int(y_mm).to_bytes(2, byteorder="little", signed=True)
            )

            print(
                f"  -> write x={x_mm} mm, y={y_mm} mm  payload={payload.hex()}")
            await client.write_gatt_char(CHAR_UUID, payload, response=False)

            await asyncio.sleep(0.5)

        print("\nDone sending test positions.")


if __name__ == "__main__":
    asyncio.run(main())
