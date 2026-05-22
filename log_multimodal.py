import serial
import serial.tools.list_ports
import time
import csv
import sys

# ==========================================================
# Configuration
# ==========================================================
BAUD_RATE = 115200
OUTPUT_FILE = "multimodal_occupancy.csv"

# The total test duration is set to 6 minutes (360 seconds)
TOTAL_DURATION_SECONDS = 360 

# Annotation reminders for your thesis experiment
PHASE_NOTES = [
    "Test starts: Room is OCCUPIED.",
    "During test: Leave the room and return later to capture all phases.",
    "Goal: Verify if Mic Trigger correctly extends the Radar state via the fusion gate."
]

# ==========================================================
# COM Port Auto-Selection
# ==========================================================
def get_com_port():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No COM ports found. Please ensure your ESP32-S3 is plugged in.")
        sys.exit(1)

    print("Available COM Ports:")
    for i, port in enumerate(ports):
        print(f"[{i}] {port.device} - {port.description}")

    if len(ports) == 1:
        print(f"Auto-selecting {ports[0].device}")
        return ports[0].device

    try:
        selection = int(input("Select the COM port index for your ESP32-S3: "))
        return ports[selection].device
    except (ValueError, IndexError):
        print("Invalid selection.")
        sys.exit(1)

# ==========================================================
# Main Logger
# ==========================================================
def main():
    print("=" * 70)
    print("  Multimodal Occupancy Logger (Radar + Mic Fusion)")
    print("=" * 70)
    print("\n[!] TEST PROCEDURE REMINDER:")
    for note in PHASE_NOTES:
        print(f"    - {note}")
    print(f"\n[!] The script will automatically stop after {TOTAL_DURATION_SECONDS/60:.1f} minutes.")
    print("[!] Press Ctrl+C at any time to stop logging early.")
    print("[!] Make sure the Arduino IDE Serial Monitor is CLOSED!\n")

    com_port = get_com_port()

    print(f"\nConnecting to {com_port} at {BAUD_RATE} baud...")
    try:
        ser = serial.Serial(com_port, BAUD_RATE, timeout=1)
    except Exception as e:
        print(f"\n[!] Failed to connect to {com_port}: {e}")
        print("[!] IMPORTANT: Close the Arduino IDE Serial Monitor first!")
        sys.exit(1)

    print(f"Successfully connected. Logging up to {TOTAL_DURATION_SECONDS} seconds...")
    print(f"Data will be saved to '{OUTPUT_FILE}'.\n")
    print("START: Logging now!")
    print("-" * 70)

    start_time = time.time()
    last_timer_print = 0

    with open(OUTPUT_FILE, mode='w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        # Header matches the columns from ESP32 output
        writer.writerow(["Timestamp(s)", "Radar_State", "Mic_Trigger", "Fused_System_State"])

        try:
            while True:
                elapsed_time = time.time() - start_time

                if elapsed_time > TOTAL_DURATION_SECONDS:
                    print(f"\n\nTime's up! {TOTAL_DURATION_SECONDS} seconds completed.")
                    break

                if ser.in_waiting > 0:
                    try:
                        line = ser.readline().decode('utf-8').strip()
                        if line:
                            # Skip ESP32 boot messages and header lines
                            if any(x in line for x in ["Timestamp", "System:", "Error:", "ets ", "rst:"]):
                                print(f"\n[ESP32]: {line}")
                                continue

                            # Expected format from ESP32: 1,0,1
                            parts = line.split(',')
                            if len(parts) == 3:
                                # Prepend the PC's elapsed time for strict consistency
                                row = [f"{elapsed_time:.2f}"] + parts
                                writer.writerow(row)
                                f.flush()  # Write immediately so no data is lost

                                radar = parts[0].strip()
                                mic   = parts[1].strip()
                                fused = parts[2].strip()

                                radar_lbl = "PRES" if radar == '1' else "----"
                                mic_lbl   = "NOISE" if mic == '1' else "-----"
                                fused_lbl = "OCCUPIED" if fused == '1' else "EMPTY   "

                                print(
                                    f"[{elapsed_time:06.1f}s] "
                                    f"Radar: {radar_lbl} | "
                                    f"Mic: {mic_lbl} | "
                                    f"Fused OUT: {fused_lbl}",
                                    end='\r'
                                )
                                last_timer_print = elapsed_time
                    except UnicodeDecodeError:
                        pass  # Ignore corrupted bytes during boot/reset

                else:
                    # Keep the timer ticking visually even without new data
                    if elapsed_time - last_timer_print > 0.2:
                        print(f"[{elapsed_time:06.1f}s] Waiting for data..." + " " * 30, end='\r')
                        last_timer_print = elapsed_time

                time.sleep(0.01)

        except KeyboardInterrupt:
            print("\n\nLogging stopped manually by user (Ctrl+C).")

    ser.close()
    print(f"\nDone. Port closed. Data securely saved to '{OUTPUT_FILE}'.")

if __name__ == "__main__":
    main()
