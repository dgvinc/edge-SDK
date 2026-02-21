"""
HRV-Synced Breathing Example
Synchronize glasses breathing pattern with heart rate

This example shows how to sync the glasses with a heart rate monitor
for coherence training / HRV biofeedback.

Requires:
    pip install edge-glasses bleak
"""

import asyncio
import time
from edge_glasses import Glasses


class HRVBreathing:
    """
    HRV-synchronized breathing trainer
    
    The glasses guide breathing at a rate optimized for HRV coherence,
    typically around 6 breaths per minute (10s per breath cycle).
    """
    
    def __init__(self):
        self.glasses = None
        self.running = False
        
        # Breathing parameters for HRV coherence
        # 6 breaths/min = 10s cycle = 5s inhale + 5s exhale
        self.inhale_time = 5.0
        self.exhale_time = 5.0
        
        # Update rate
        self.update_hz = 30
    
    async def connect(self):
        """Connect to glasses"""
        print("Connecting to EDGE Glasses...")
        self.glasses = Glasses()
        await self.glasses.connect()
        print("Connected!")
    
    async def disconnect(self):
        """Disconnect and clear"""
        if self.glasses:
            await self.glasses.clear()
            await self.glasses.disconnect()
    
    async def breathing_cycle(self):
        """
        Run one breathing cycle
        Returns elapsed time
        """
        cycle_time = self.inhale_time + self.exhale_time
        update_interval = 1.0 / self.update_hz
        
        # Inhale phase: clear -> dark
        print("  Inhale...", end="", flush=True)
        for i in range(int(self.inhale_time * self.update_hz)):
            progress = i / (self.inhale_time * self.update_hz)
            opacity = int(progress * 255)
            await self.glasses.set_opacity(opacity)
            await asyncio.sleep(update_interval)
        print(" done")
        
        # Exhale phase: dark -> clear
        print("  Exhale...", end="", flush=True)
        for i in range(int(self.exhale_time * self.update_hz)):
            progress = i / (self.exhale_time * self.update_hz)
            opacity = int((1 - progress) * 255)
            await self.glasses.set_opacity(opacity)
            await asyncio.sleep(update_interval)
        print(" done")
        
        return cycle_time
    
    async def run(self, duration_minutes: float = 5.0):
        """
        Run HRV breathing session
        
        Args:
            duration_minutes: Session duration
        """
        print(f"\nHRV Coherence Breathing - {duration_minutes} minutes")
        print(f"Breath rate: {60 / (self.inhale_time + self.exhale_time):.1f} breaths/min")
        print("-" * 40)
        
        self.running = True
        start_time = time.time()
        target_time = duration_minutes * 60
        breath_count = 0
        
        try:
            while self.running and (time.time() - start_time) < target_time:
                breath_count += 1
                elapsed = time.time() - start_time
                remaining = target_time - elapsed
                
                print(f"\nBreath {breath_count} ({remaining:.0f}s remaining)")
                await self.breathing_cycle()
        
        finally:
            self.running = False
            await self.glasses.clear()
        
        print(f"\nSession complete! {breath_count} breaths")
    
    def stop(self):
        """Stop the session"""
        self.running = False


async def main():
    print("EDGE Glasses - HRV Coherence Breathing")
    print("=" * 40)
    print()
    print("This session guides breathing at 6 breaths/minute,")
    print("the optimal rate for heart rate variability coherence.")
    print()
    
    hrv = HRVBreathing()
    
    try:
        await hrv.connect()
        
        duration = input("Duration in minutes (default 5): ").strip()
        duration = float(duration) if duration else 5.0
        
        input("Press Enter to start...")
        await hrv.run(duration_minutes=duration)
        
    except KeyboardInterrupt:
        print("\nStopped!")
    finally:
        await hrv.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
