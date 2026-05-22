import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import os

# 1. Load Data
file_path = 'dynamic_tracking_data.csv'

try:
    df = pd.read_csv(file_path)
except FileNotFoundError:
    print(f"Error: Could not find '{file_path}'. Please ensure the file is in the same directory.")
    exit(1)

# Generate a continuous time array in seconds based on the 500ms logging rate
df['Calculated_Time(s)'] = np.arange(len(df)) * 0.5

# Setup plotting style parameters for clean academic/corporate look
plt.rcParams.update({
    'font.size': 11,
    'axes.labelsize': 12,
    'axes.titlesize': 14,
    'xtick.labelsize': 11,
    'ytick.labelsize': 11,
    'legend.fontsize': 11,
    'figure.titlesize': 16,
    'font.family': 'sans-serif'
})

# 2. Create a stacked 2-panel subplot layout (sharing X-axis)
fig, (ax1, ax2) = plt.subplots(nrows=2, ncols=1, figsize=(12, 8), sharex=True)
fig.suptitle('Dynamic Light Sensor Tracking Analysis (Condition B)', fontweight='bold', y=0.96)

# Configuration for grid lines
grid_kwargs = {'linestyle': '--', 'linewidth': 0.7, 'alpha': 0.6, 'color': '#aaaaaa'}

# Phase labels and marker positions
phase_markers = [20.0, 40.0]
phase_labels = [
    (10.0, 'Low-Frequency Sweeps'),
    (30.0, 'High-Frequency Waves'),
    (50.0, 'Transient Step Changes')
]

# --- Top Panel: Digital Lux ---
ax1.plot(df['Calculated_Time(s)'], df['Lux'], color='#1f77b4', linewidth=1.8, label='Digital Lux (VEML7700)')
ax1.set_ylabel('Illuminance (Lux)', fontweight='bold')
ax1.grid(True, **grid_kwargs)
ax1.legend(loc='upper right', framealpha=0.9)

# Draw vertical dashed marker lines
for marker in phase_markers:
    ax1.axvline(x=marker, color='#333333', linestyle='-.', linewidth=1.5, alpha=0.8)

# Add text labels floating near the top (using axis coordinates for Y to stay consistently at the top)
for x_pos, text in phase_labels:
    ax1.text(x_pos, 0.93, text, ha='center', va='top', transform=ax1.get_xaxis_transform(),
             bbox=dict(facecolor='white', alpha=0.85, edgecolor='#cccccc', boxstyle='round,pad=0.4'),
             fontweight='bold', color='#222222', fontsize=10)

# --- Bottom Panel: Analog Voltage ---
ax2.plot(df['Calculated_Time(s)'], df['Voltage(V)'], color='#ff7f0e', linewidth=1.8, label='Analog Voltage (A0)')
ax2.set_xlabel('Time (Seconds)', fontweight='bold')
ax2.set_ylabel('Voltage (V)', fontweight='bold')
ax2.grid(True, **grid_kwargs)
ax2.legend(loc='upper right', framealpha=0.9)

# Draw vertical dashed marker lines
for marker in phase_markers:
    ax2.axvline(x=marker, color='#333333', linestyle='-.', linewidth=1.5, alpha=0.8)

# Add text labels floating near the top
for x_pos, text in phase_labels:
    ax2.text(x_pos, 0.93, text, ha='center', va='top', transform=ax2.get_xaxis_transform(),
             bbox=dict(facecolor='white', alpha=0.85, edgecolor='#cccccc', boxstyle='round,pad=0.4'),
             fontweight='bold', color='#222222', fontsize=10)

# Final adjustments
# Set X-axis limit to capture the full 60 seconds of the test smoothly
max_time = max(60.0, df['Calculated_Time(s)'].max())
plt.xlim(0, max_time)
plt.tight_layout(rect=[0, 0, 1, 0.95])

# Save the final figure as a high-resolution PNG
output_filename = 'dynamic_sensor_tracking.png'
plt.savefig(output_filename, dpi=300, bbox_inches='tight')
print(f"\nPlot successfully generated and saved as '{output_filename}' at 300 DPI.")
