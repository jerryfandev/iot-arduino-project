import pandas as pd
import matplotlib.pyplot as plt

# 1. Load Data
file_path = 'test_b.csv'
try:
    df = pd.read_csv(file_path)
except FileNotFoundError:
    print(f"Error: Could not find '{file_path}'. Please ensure you run this script in the project directory.")
    exit(1)

# Ensure 'Timestamp(s)' is used for the X-axis
if 'Timestamp(s)' in df.columns:
    time_col = df['Timestamp(s)']
else:
    time_col = df.index * 0.5 # Fallback to index if timestamp is missing

# Setup clean academic plotting style
plt.rcParams.update({
    'font.size': 11,
    'axes.labelsize': 12,
    'axes.titlesize': 13,
    'legend.fontsize': 11,
    'figure.titlesize': 16,
    'font.family': 'sans-serif'
})

# 2. Create 2-panel subplot (sharing X-axis)
fig, (ax1, ax2) = plt.subplots(nrows=2, ncols=1, figsize=(10, 8), sharex=True)
fig.suptitle('Sensor Saturation Test: Phone Light Direct Exposure', fontweight='bold', y=0.96)

grid_kwargs = {'linestyle': '--', 'linewidth': 0.7, 'alpha': 0.6, 'color': '#aaaaaa'}

# --- Top Panel: Analog Input (Clipping Check) ---
ax1.plot(time_col, df['RawAnalog'], color='#ff7f0e', linewidth=2, label='Raw Analog Sensor (A0)')

# Draw a red horizontal line precisely at 4095 to visualize the 12-bit ADC ceiling/flatlining
ax1.axhline(y=4095, color='red', linestyle='--', linewidth=2, alpha=0.8, label='12-bit ADC Hardware Ceiling (4095)')

ax1.set_title('Analog Sensor Saturation (ADC Clipping)', fontweight='bold', fontsize=12)
ax1.set_ylabel('Raw ADC Value (0-4095)', fontweight='bold')
# Set Y-axis limits slightly above 4095 so the ceiling is visually obvious
ax1.set_ylim(max(0, df['RawAnalog'].min() - 200), 4200) 
ax1.grid(True, **grid_kwargs)
ax1.legend(loc='lower right', framealpha=0.9)

# --- Bottom Panel: Digital Lux (VEML Response) ---
# This shows how the digital sensor handles the same extreme lighting without clipping
ax2.plot(time_col, df['Lux'], color='#1f77b4', linewidth=2, label='Digital Lux Sensor (VEML)')

ax2.set_title('Digital Sensor Dynamic Range', fontweight='bold', fontsize=12)
ax2.set_xlabel('Time (Seconds)', fontweight='bold')
ax2.set_ylabel('Illuminance (Lux)', fontweight='bold')
# Start Y-axis at 0 to emphasize the massive dynamic scale the digital sensor can handle
ax2.set_ylim(bottom=0, top=df['Lux'].max() * 1.1) 
ax2.grid(True, **grid_kwargs)
ax2.legend(loc='lower right', framealpha=0.9)

# Final layout adjustments
plt.xlim(time_col.min(), time_col.max())
plt.tight_layout(rect=[0, 0, 1, 0.95])

# Save high-resolution figure
output_filename = 'saturation_test.png'
plt.savefig(output_filename, dpi=300, bbox_inches='tight')
print(f"\nSaturation plot successfully generated and saved as '{output_filename}' at 300 DPI.")
