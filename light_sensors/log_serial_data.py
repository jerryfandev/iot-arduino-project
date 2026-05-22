import serial
import serial.tools.list_ports
import time
import csv
import sys

# Configuration
BAUD_RATE = 115200
DURATION_MINUTES = 1
DURATION_SECONDS = DURATION_MINUTES * 30
OUTPUT_FILE = "test_b.csv"

def get_com_port():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No COM ports found. Please ensure your ESP32 is plugged in.")
        sys.exit(1)
    
    print("Available COM Ports:")
    for i, port in enumerate(ports):
        print(f"[{i}] {port.device} - {port.description}")
        
    if len(ports) == 1:
        print(f"Auto-selecting {ports[0].device}")
        return ports[0].device

    try:
        selection = int(input("Select the COM port index for your ESP32: "))
        return ports[selection].device
    except (ValueError, IndexError):
        print("Invalid selection.")
        sys.exit(1)

def main():
    print("========================================")
    print(f"  ESP32 Serial Data Logger ({DURATION_MINUTES} Minutes) ")
    print("========================================")
    com_port = get_com_port()
    
    print(f"\nConnecting to {com_port} at {BAUD_RATE} baud...")
    
    try:
        ser = serial.Serial(com_port, BAUD_RATE, timeout=1)
    except Exception as e:
        print(f"\n[!] Failed to connect to {com_port}: {e}")
        print("[!] IMPORTANT: Make sure the Serial Monitor in Arduino IDE is CLOSED before running this script!")
        sys.exit(1)
        
    print(f"Successfully connected. Logging data for {DURATION_MINUTES} minutes...")
    print(f"Data will be saved to '{OUTPUT_FILE}' in the current directory.")
    
    start_time = time.time()
    
    with open(OUTPUT_FILE, mode='w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        # Adding a timestamp column to your existing header
        writer.writerow(["Timestamp(s)", "Lux", "RawAnalog", "Voltage(V)"])
        
        try:
            while True:
                elapsed_time = time.time() - start_time
                if elapsed_time > DURATION_SECONDS:
                    print("\n\nTime's up! 1 minute of logging completed.")
                    break
                    
                if ser.in_waiting > 0:
                    try:
                        line = ser.readline().decode('utf-8').strip()
                        if line:
                            # Skip the header or boot messages printed by the ESP32
                            if "Lux" in line or "System:" in line or "Error:" in line or "ets " in line or "rst:" in line:
                                print(f"\n[ESP32]: {line}")
                                continue
                            
                            # Parse the CSV line from ESP32
                            parts = line.split(',')
                            if len(parts) == 3:
                                # Prepend the elapsed time
                                row = [f"{elapsed_time:.2f}"] + parts
                                writer.writerow(row)
                                # Force write to disk immediately so data isn't lost if stopped early
                                f.flush()
                                
                                # Print progress on the same line
                                print(f"[{elapsed_time:05.1f}s / {DURATION_SECONDS}s] Logged: {line}", end='\r')
                    except UnicodeDecodeError:
                        pass # Ignore corrupted serial bytes during boot/reset
                        
        except KeyboardInterrupt:
            print("\n\nLogging stopped manually by user (Ctrl+C).")
            
    ser.close()
    print(f"Done. Port closed. All data securely stored in '{OUTPUT_FILE}'.")

if __name__ == "__main__":
    main()
