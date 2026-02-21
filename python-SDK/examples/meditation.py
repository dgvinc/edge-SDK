"""
Meditation Session Example
Demonstrates timed session with breathing patterns
"""

import asyncio
from edge_glasses import Glasses


async def main():
    print("EDGE Glasses - Meditation Session")
    print("=" * 40)
    
    async with Glasses() as glasses:
        print("Connected!")
        print()
        
        # Choose session type
        print("Session types:")
        print("  1. Relax (15 min) - slow strobe, deep breathing")
        print("  2. Focus (10 min) - faster strobe, quick breathing")
        print("  3. Meditate (10 min) - balanced settings")
        print("  4. Sleep (20 min) - very slow, auto-sleep after")
        print("  5. Custom session")
        print()
        
        choice = input("Select (1-5): ").strip()
        
        if choice == "1":
            print("Starting relaxation session...")
            await glasses.session_relax(duration=15)
            
        elif choice == "2":
            print("Starting focus session...")
            await glasses.session_focus(duration=10)
            
        elif choice == "3":
            print("Starting meditation session...")
            await glasses.session_meditate(duration=10)
            
        elif choice == "4":
            print("Starting sleep session...")
            await glasses.session_sleep(duration=20)
            
        elif choice == "5":
            # Custom session
            duration = int(input("Duration (minutes): ") or "10")
            strobe_start = int(input("Start Hz (1-50): ") or "12")
            strobe_end = int(input("End Hz (1-50): ") or "8")
            inhale = float(input("Inhale seconds: ") or "4.0")
            exhale = float(input("Exhale seconds: ") or "4.0")
            
            print(f"Starting {duration}-minute custom session...")
            await glasses.start_session(
                duration=duration,
                strobe_start=strobe_start,
                strobe_end=strobe_end,
                inhale=inhale,
                hold_in_end=inhale,  # Hold grows to match inhale
                exhale=exhale,
                hold_out_end=exhale,  # Hold grows to match exhale
            )
        
        else:
            print("Invalid choice")
            return
        
        print()
        print("Session started! The glasses will auto-sleep when done.")
        print("Close the glasses arms to sleep early.")


if __name__ == "__main__":
    asyncio.run(main())
