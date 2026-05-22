import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# 1. Load Data
# Assuming the script is run in the same directory as the CSV
try:
    df = pd.read_csv('luminance_data.csv')
except FileNotFoundError:
    print("Error: 'luminance_data.csv' not found in the current directory.")
    exit(1)

# Generate a continuous time array in seconds based on the 500ms logging rate
# This ensures perfectly equidistant time steps, ignoring any slight serial UART transmission delays
df['Calculated_Time(s)'] = np.arange(len(df)) * 0.5

# 2. Calculate Statistical Metrics
def calc_metrics(series):
    mean_val = series.mean()
    std_val = series.std()
    ptp_val = series.max() - series.min()
    rsd_val = (std_val / mean_val) * 100 if mean_val != 0 else 0
    return mean_val, std_val, ptp_val, rsd_val

lux_mean, lux_std, lux_ptp, lux_rsd = calc_metrics(df['Lux'])
vol_mean, vol_std, vol_ptp, vol_rsd = calc_metrics(df['Voltage(V)'])

# 3. Print Clean Summary Table
print("\n" + "=" * 65)
print(f"{'LUMINANCE SENSOR BENCHMARK METRICS (5-Minute Baseline)':^65}")
print("=" * 65)
print(f"{'Metric':<25} | {'Digital Lux (VEML7700)':<17} | {'Analog Voltage (V)':<17}")
print("-" * 65)
print(f"{'Mean':<25} | {lux_mean:>17.4f} | {vol_mean:>17.4f}")
print(f"{'Standard Dev (RMS Noise)':<25} | {lux_std:>17.4f} | {vol_std:>17.4f}")
print(f"{'Peak-to-Peak (Max-Min)':<25} | {lux_ptp:>17.4f} | {vol_ptp:>17.4f}")
print(f"{'Relative Std Dev (RSD %)':<25} | {lux_rsd:>16.4f}% | {vol_rsd:>16.4f}%")
print("=" * 65 + "\n")

# 4. Generate 4-panel subplot figure
fig, axes = plt.subplots(nrows=2, ncols=2, figsize=(14, 10))
fig.suptitle('Luminance Sensor Signal Conditioning Efficiency & Noise Analysis', 
             fontsize=16, fontweight='bold', y=0.96)

# Configuration for grid lines to be subtly included for an engineering thesis
grid_kwargs = {'linestyle': '--', 'linewidth': 0.7, 'alpha': 0.6, 'color': 'gray'}

# --- Upper Left: Digital Lux time-series line plot ---
ax1 = axes[0, 0]
ax1.plot(df['Calculated_Time(s)'], df['Lux'], color='#1f77b4', linewidth=1.5, label='VEML7700 Lux')
ax1.set_title('Digital Lux Time-Series', fontweight='bold', fontsize=12)
ax1.set_xlabel('Time (s)', fontsize=11)
ax1.set_ylabel('Illuminance (Lux)', fontsize=11)
ax1.grid(True, **grid_kwargs)
ax1.legend(loc='upper right')

# --- Upper Right: Digital Lux histogram ---
ax2 = axes[0, 1]
ax2.hist(df['Lux'], bins=30, color='#1f77b4', edgecolor='black', alpha=0.7, label='Digital Lux')
ax2.set_title('Digital Lux Distribution Profile', fontweight='bold', fontsize=12)
ax2.set_xlabel('Illuminance (Lux)', fontsize=11)
ax2.set_ylabel('Frequency / Count', fontsize=11)
ax2.grid(True, **grid_kwargs)
ax2.legend(loc='upper right')

# --- Lower Left: Analog Voltage time-series line plot ---
ax3 = axes[1, 0]
ax3.plot(df['Calculated_Time(s)'], df['Voltage(V)'], color='#ff7f0e', linewidth=1.5, label='Raw Analog A0')
ax3.set_title('Analog Voltage Time-Series', fontweight='bold', fontsize=12)
ax3.set_xlabel('Time (s)', fontsize=11)
ax3.set_ylabel('Voltage (V)', fontsize=11)
ax3.grid(True, **grid_kwargs)
ax3.legend(loc='upper right')

# --- Lower Right: Analog Voltage histogram ---
ax4 = axes[1, 1]
ax4.hist(df['Voltage(V)'], bins=30, color='#ff7f0e', edgecolor='black', alpha=0.7, label='Analog Voltage')
ax4.set_title('Analog Voltage Noise Distribution', fontweight='bold', fontsize=12)
ax4.set_xlabel('Voltage (V)', fontsize=11)
ax4.set_ylabel('Frequency / Count', fontsize=11)
ax4.grid(True, **grid_kwargs)
ax4.legend(loc='upper right')

# Adjust layout cleanly
plt.tight_layout(rect=[0, 0, 1, 0.95])

# Save as high-resolution PNG
output_filename = 'luminance_analysis_plots.png'
plt.savefig(output_filename, dpi=300, bbox_inches='tight')
print(f"Subplot figure successfully generated and saved as '{output_filename}' (300 DPI).")
