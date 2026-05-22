import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import os

# Set seed for reproducibility
np.random.seed(42)

# ==============================================================================
# 1. DATA GENERATION & HARDWARE MATHEMATICAL MODELING
# ==============================================================================
time_step = 0.5  # seconds
t = np.arange(0, 120, time_step)
n_samples = len(t)

lux = np.zeros(n_samples)
for i, time_val in enumerate(t):
    if time_val < 30.0:
        # Full Shading: Tight distribution around 92 Lux
        lux[i] = np.random.normal(92, 2)
    elif time_val < 90.0:
        # Dynamic Adaptation: Sine wave between 170 and 430 Lux
        # Center = 300, Amplitude = 130
        # Frequency = 1 cycle per 15 seconds (4 cycles across the 60s window)
        lux[i] = 300 + 130 * np.sin(2 * np.pi * (time_val - 30) / 15) + np.random.normal(0, 5)
    else:
        # Peak Solar Saturation: Spike hovering around 540 Lux
        lux[i] = np.random.normal(540, 4)

# Closed-Loop Firmware Mapping
# 100 Lux -> 255 Brightness | 500 Lux -> 0 Brightness
brightness = 255 - ((lux - 100) / 400.0) * 255
brightness = np.clip(brightness, 0, 255).astype(int)

# WS2812 Power Model
# 30 LEDs, 5.0V bus
# Pixel current: 1.0mA base + 49.0mA * (brightness / 255)
pixel_current_A = (1.0 + 49.0 * (brightness / 255.0)) / 1000.0
total_current_A = 30 * pixel_current_A
instant_power_W = 5.0 * total_current_A

# System Efficiency Audit
# Baseline unautomated system: 30 LEDs * 50mA = 1.5A -> 5V * 1.5A = 7.5W
baseline_power_W = 7.5 
saved_power_W = baseline_power_W - instant_power_W

# Integration (Energy saved in Joules = Power (W) * time (s))
energy_saved_J_per_step = saved_power_W * time_step
cumulative_joules = np.cumsum(energy_saved_J_per_step)
accumulated_wh = cumulative_joules / 3600.0

# Compile Dataset
df = pd.DataFrame({
    'Timestamp(s)': t,
    'Lux': np.round(lux, 2),
    'LED_Brightness': brightness,
    'InstantPower_W': np.round(instant_power_W, 3),
    'SavedPower_W': np.round(saved_power_W, 3),
    'AccumulatedSavings_Wh': np.round(accumulated_wh, 6)
})

# ==============================================================================
# 3. EXPORT & CONSOLE AUDIT
# ==============================================================================
csv_filename = "power_efficiency_data.csv"
df.to_csv(csv_filename, index=False)

total_duration = t[-1] + time_step
max_power_saved = saved_power_W.max()
total_joules = cumulative_joules[-1]
total_wh = accumulated_wh[-1]
avg_reduction_pct = (saved_power_W.mean() / baseline_power_W) * 100

print("\n" + "="*70)
print(f"{'SOFTWARE-MODELED DAYLIGHT HARVESTING EFFICIENCY AUDIT':^70}")
print("="*70)
print(f"Data successfully generated and exported to '{csv_filename}'\n")
print(f"{'Total Simulation Duration':<45}: {total_duration:.1f} seconds")
print(f"{'Maximum Instantaneous Power Saved':<45}: {max_power_saved:.3f} Watts")
print(f"{'Total Cumulative Energy Saved':<45}: {total_joules:.2f} Joules")
print(f"{'Total Cumulative Energy Saved':<45}: {total_wh:.6f} Watt-hours")
print(f"{'Average Power Consumption Reduction':<45}: {avg_reduction_pct:.1f}% vs Fixed 7.5W")
print("="*70 + "\n")

# ==============================================================================
# 4. VISUALIZATION SUBPLOTS
# ==============================================================================
plt.rcParams.update({
    'font.size': 11,
    'axes.labelsize': 12,
    'axes.titlesize': 13,
    'legend.fontsize': 10,
    'figure.titlesize': 16,
    'font.family': 'sans-serif'
})

fig, (ax1, ax2) = plt.subplots(nrows=2, ncols=1, figsize=(12, 9), sharex=True)
fig.suptitle('Adaptive Daylight Harvesting Power Simulation & Efficiency', fontweight='bold', y=0.96)

grid_kwargs = {'linestyle': '--', 'linewidth': 0.7, 'alpha': 0.6, 'color': '#aaaaaa'}

phase_markers = [30.0, 90.0]
phase_labels = [
    (15.0, 'Full Shading'),
    (60.0, 'Dynamic Adaptation'),
    (105.0, 'Solar Saturation')
]

# --- Upper Panel: LED Brightness & Lux (Twin Axis) ---
ax1.set_title('Closed-Loop Environmental Hardware Response', fontweight='bold', fontsize=13)
ax1.set_ylabel('LED Brightness (8-Bit PWM: 0-255)', fontweight='bold', color='#1f77b4')

line1 = ax1.plot(df['Timestamp(s)'], df['LED_Brightness'], color='#1f77b4', linewidth=2.5, label='LED Brightness')
ax1.tick_params(axis='y', labelcolor='#1f77b4')
ax1.set_ylim(-10, 265) # Slight padding for visual clarity
ax1.grid(True, **grid_kwargs)

ax1_twin = ax1.twinx()
ax1_twin.set_ylabel('Digital Sensor Illuminance (Lux)', fontweight='bold', color='#ff7f0e')
line2 = ax1_twin.plot(df['Timestamp(s)'], df['Lux'], color='#ff7f0e', linewidth=2, linestyle='-', alpha=0.9, label='VEML7700 Lux')
ax1_twin.tick_params(axis='y', labelcolor='#ff7f0e')

# Combine legends for top panel
lines = line1 + line2
labels = [l.get_label() for l in lines]
ax1.legend(lines, labels, loc='lower left', framealpha=0.95)

# Boundary Markers and Text (Upper Panel)
for marker in phase_markers:
    ax1.axvline(x=marker, color='#333333', linestyle='-.', linewidth=1.5, alpha=0.8)

for x_pos, text in phase_labels:
    ax1.text(x_pos, 0.92, text, ha='center', va='top', transform=ax1.get_xaxis_transform(),
             bbox=dict(facecolor='white', alpha=0.9, edgecolor='#cccccc', boxstyle='round,pad=0.4'),
             fontweight='bold', color='#222222', fontsize=10)

# --- Lower Panel: Power Consumption & Integration ---
ax2.set_title('System Power Profile & Calculated Energy Integration', fontweight='bold', fontsize=13)
ax2.set_xlabel('Time (Seconds)', fontweight='bold')
ax2.set_ylabel('Instantaneous Power (Watts)', fontweight='bold')
ax2.grid(True, **grid_kwargs)

# Baseline ceiling line
ax2.axhline(y=baseline_power_W, color='#d62728', linestyle='--', linewidth=2, alpha=0.9, label='7.5W Unautomated Baseline')

# Active Power curve
ax2.plot(df['Timestamp(s)'], df['InstantPower_W'], color='#2ca02c', linewidth=2.5, label='Active Adaptive Power Load (W)')

# Fill area from the baseline ceiling down to the active power curve
ax2.fill_between(df['Timestamp(s)'], df['InstantPower_W'], baseline_power_W, color='#2ca02c', alpha=0.25, label='Integrated Energy Saved')

ax2.set_ylim(0, 8.5) # Provide headroom above the 7.5W baseline
ax2.legend(loc='lower right', framealpha=0.95)

# Boundary Markers and Text (Lower Panel)
for marker in phase_markers:
    ax2.axvline(x=marker, color='#333333', linestyle='-.', linewidth=1.5, alpha=0.8)

for x_pos, text in phase_labels:
    ax2.text(x_pos, 0.92, text, ha='center', va='top', transform=ax2.get_xaxis_transform(),
             bbox=dict(facecolor='white', alpha=0.9, edgecolor='#cccccc', boxstyle='round,pad=0.4'),
             fontweight='bold', color='#222222', fontsize=10)

plt.xlim(0, 120.0)
plt.tight_layout(rect=[0, 0, 1, 0.95])

# ==============================================================================
# 5. EXPORT PLOT
# ==============================================================================
output_filename = 'lighting_power_efficiency_analysis.png'
plt.savefig(output_filename, dpi=300, bbox_inches='tight')
print(f"Publication-ready subplot generated and saved as '{output_filename}' at 300 DPI.")
