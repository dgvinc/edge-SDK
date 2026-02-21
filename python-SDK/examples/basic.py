"""
Basic EDGE Glasses Example
Demonstrates simple opacity control
"""

import asyncio
from edge_glasses import Glasses


async def main():
    print("Scanning for EDGE Glasses...")
    devices = await Glasses.scan(timeout=5.0)
    
    if not devices:
        print("No devices found!")
        return
    
    print(f"Found: {devices[0]}")
    
    async with Glasses() as glasses:
        print("Connected!")
        
        # Cycle through opacity levels
        print("Cycling opacity...")
        for opacity in range(0, 256, 32):
            print(f"  Opacity: {opacity}")
            await glasses.set_opacity(opacity)
            await asyncio.sleep(0.5)
        
        # Back to clear
        print("Clearing...")
        await glasses.clear()
        await asyncio.sleep(1)
        
        print("Done!")


if __name__ == "__main__":
    asyncio.run(main())
