#!/usr/bin/env python3
"""
EDGE Glasses CLI
Command-line control for EDGE Smart Glasses

Usage:
    edge-glasses scan              # Find devices
    edge-glasses opacity 128       # Set opacity (0-255)
    edge-glasses clear             # Clear lenses
    edge-glasses dark              # Darken lenses
    edge-glasses session relax 10  # 10-min relax session
    edge-glasses sleep             # Put to sleep
"""

import asyncio
import sys
from edge_glasses import Glasses


async def cmd_scan():
    """Scan for devices"""
    print("Scanning for EDGE Glasses...")
    devices = await Glasses.scan(timeout=5.0)
    
    if not devices:
        print("No devices found.")
        return
    
    print(f"Found {len(devices)} device(s):")
    for i, d in enumerate(devices):
        print(f"  {i+1}. {d.name} [{d.address}] RSSI: {d.rssi}")


async def cmd_opacity(value: int):
    """Set opacity"""
    async with Glasses() as g:
        await g.set_opacity(value)
        print(f"Opacity set to {value}")


async def cmd_clear():
    """Clear lenses"""
    async with Glasses() as g:
        await g.clear()
        print("Lenses cleared")


async def cmd_dark():
    """Darken lenses"""
    async with Glasses() as g:
        await g.dark()
        print("Lenses darkened")


async def cmd_hold(duty: int):
    """Hold at duty cycle"""
    async with Glasses() as g:
        await g.hold(duty)
        print(f"Holding at {duty}%")


async def cmd_session(session_type: str, duration: int):
    """Start session"""
    async with Glasses() as g:
        if session_type == "relax":
            await g.session_relax(duration)
        elif session_type == "focus":
            await g.session_focus(duration)
        elif session_type == "meditate":
            await g.session_meditate(duration)
        elif session_type == "sleep":
            await g.session_sleep(duration)
        else:
            print(f"Unknown session type: {session_type}")
            print("Options: relax, focus, meditate, sleep")
            return
        print(f"Started {session_type} session ({duration} min)")


async def cmd_sleep():
    """Sleep device"""
    async with Glasses() as g:
        await g.sleep()
        print("Device sleeping")


async def cmd_resume():
    """Resume session"""
    async with Glasses() as g:
        await g.resume()
        print("Session resumed")


def print_help():
    print(__doc__)
    print("Commands:")
    print("  scan                     Scan for devices")
    print("  opacity <0-255>          Set opacity")
    print("  clear                    Clear lenses (opacity 0)")
    print("  dark                     Darken lenses (opacity 255)")
    print("  hold <0-100>             Hold at duty cycle %")
    print("  session <type> <mins>    Start session (relax/focus/meditate/sleep)")
    print("  resume                   Resume/restart session")
    print("  sleep                    Put device to sleep")


async def main():
    if len(sys.argv) < 2:
        print_help()
        return
    
    cmd = sys.argv[1].lower()
    
    try:
        if cmd == "scan":
            await cmd_scan()
        
        elif cmd == "opacity":
            if len(sys.argv) < 3:
                print("Usage: edge-glasses opacity <0-255>")
                return
            await cmd_opacity(int(sys.argv[2]))
        
        elif cmd == "clear":
            await cmd_clear()
        
        elif cmd == "dark":
            await cmd_dark()
        
        elif cmd == "hold":
            if len(sys.argv) < 3:
                print("Usage: edge-glasses hold <0-100>")
                return
            await cmd_hold(int(sys.argv[2]))
        
        elif cmd == "session":
            if len(sys.argv) < 4:
                print("Usage: edge-glasses session <type> <minutes>")
                print("Types: relax, focus, meditate, sleep")
                return
            await cmd_session(sys.argv[2].lower(), int(sys.argv[3]))
        
        elif cmd == "resume":
            await cmd_resume()
        
        elif cmd == "sleep":
            await cmd_sleep()
        
        elif cmd in ("help", "-h", "--help"):
            print_help()
        
        else:
            print(f"Unknown command: {cmd}")
            print_help()
    
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


def cli_main():
    """Synchronous entry point for CLI"""
    asyncio.run(main())


if __name__ == "__main__":
    cli_main()
