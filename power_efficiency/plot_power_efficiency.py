import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# 1. Load Data
file_path = 'power_efficiency_data.csv'
try:
    df = pd.read_csv(file_path)
except FileNotFoundError:
    print(f"Error: Could not find '{file_path}'. Please ensure the file is in the project directory.")
    exit(1)

# Generate a continuous time array in seconds using the 500ms logging rate
df['Time(s)'] = np.arange(len(df)) * 0.5

# 2. Calculate summary metrics
# Duration is total samples * 0.5s
total_duration = len(df) * 0.5
max_power_saved = df['SavedPower_W'].max()
# Cumulative energy saved is the final value of the accumulated column
total_energy_wh = df['AccumulatedSavings_Wh'].iloc[-1]
# Convert Watt-hours to Joules (1 Wh = 3600 Joules)
total_energy_j = total_energy_wh * 3600

# Compute average power reduction percentage
# Theoretical baseline power (unautomated 100% brightness) = InstantPower_W + SavedPower_W
df['BaselinePower_W'] = df['InstantPower_W'] + df['SavedPower_W']
mean_saved = df['SavedPower_W'].mean()
mean_baseline = df['BaselinePower_W'].mean()
avg_reduction_pct = (mean_saved / mean_baseline) * 100 if mean_baseline > 0 else 0.0

# 3. Print clean academic summary block to terminal
print("\n" + "="*65)
print(f"{'ADAPTIVE DAYLIGHT HARVESTING EFFICIENCY SUMMARY':^65}")
print("="*65)
print(f"{'Total Testing Duration':<40}: {total_duration:.1f} seconds")
print(f"{'Max Instantaneous Power Saved':<40}: {max_power_saved:.3f} W")
print(f"{'Total Cumulative Energy Saved':<40}: {total_energy_wh:.6f} Wh ({total_energy_j:.2f} Joules)")
print(f"{'Avg Power Reduction vs 100% Baseline':<40}: {avg_reduction_pct:.1f}%")
print("="*65 + "\n")

# 4. Generate professional dual-panel stacked subplot figure
plt.rcParams.update({
    'font.size': 11,
    'axes.labelsize': 12,
    'axes.titlesize': 13,
    'legend.fontsize': 10,
    'figure.titlesize': 16,
    'font.family': 'sans-serif'
})

fig, (ax1, ax2) = plt.subplots(nrows=2, ncols=1, figsize=(12, 9), sharex=True)
fig.suptitle('Adaptive Daylight Harvesting Power Efficiency Analysis', fontweight='bold', y=0.96)

grid_kwargs = {'linestyle': '--', 'linewidth': 0.7, 'alpha': 0.6, 'color': '#aaaaaa'}

# Phase boundaries and label configurations
phase_markers = [30.0, 90.0]
phase_labels = [
    (15.0, 'Full Shading'),
    (60.0, 'Dynamic Adaptation'),
    (105.0, 'Solar Saturation')
]

# --- Top Panel: Closed-Loop Feedback (Brightness vs Lux) ---
ax1.set_title('Closed-Loop Environmental Adaptation', fontweight='bold', fontsize=13)
ax1.set_ylabel('LED Brightness (0-255)', fontweight='bold', color='#1f77b4')

# Plot Brightness on the primary Y-axis
line1 = ax1.plot(df['Time(s)'], df['LED_Brightness'], color='#1f77b4', linewidth=2, label='LED Brightness')
ax1.tick_params(axis='y', labelcolor='#1f77b4')
ax1.grid(True, **grid_kwargs)

# Plot Lux on the secondary Y-axis
ax1_twin = ax1.twinx()
ax1_twin.set_ylabel('Environmental Illuminance (Lux)', fontweight='bold', color='#ff7f0e')
line2 = ax1_twin.plot(df['Time(s)'], df['Lux'], color='#ff7f0e', linewidth=2, linestyle='-', alpha=0.8, label='Sensor Lux')
ax1_twin.tick_params(axis='y', labelcolor='#ff7f0e')

# Combine legends cleanly for both axes
lines = line1 + line2
labels = [l.get_label() for l in lines]
ax1.legend(lines, labels, loc='upper right', framealpha=0.9)

# 5. Draw vertical dashed marker lines and annotations
for marker in phase_markers:
    ax1.axvline(x=marker, color='#333333', linestyle='-.', linewidth=1.5, alpha=0.8)

for x_pos, text in phase_labels:
    # Use axis coordinates (Y=0.92) to keep text perfectly aligned horizontally near the top
    ax1.text(x_pos, 0.92, text, ha='center', va='top', transform=ax1.get_xaxis_transform(),
             bbox=dict(facecolor='white', alpha=0.85, edgecolor='#cccccc', boxstyle='round,pad=0.4'),
             fontweight='bold', color='#222222', fontsize=10)

# --- Bottom Panel: Power Consumption & Energy Integration ---
ax2.set_title('System Power Profile & Energy Savings Integration', fontweight='bold', fontsize=13)
ax2.set_xlabel('Time (Seconds)', fontweight='bold')
ax2.set_ylabel('Power (Watts)', fontweight='bold')
ax2.grid(True, **grid_kwargs)

# Plot Instant Power Load
ax2.plot(df['Time(s)'], df['InstantPower_W'], color='#d62728', linewidth=2, label='Active Power Load (W)')

# Fill the area under the Saved Power curve to visually represent integrated energy (Joules/Wh)
ax2.fill_between(df['Time(s)'], 0, df['SavedPower_W'], color='#2ca02c', alpha=0.3, label='Integrated Energy Saved')
ax2.plot(df['Time(s)'], df['SavedPower_W'], color='#2ca02c', linewidth=1.5, linestyle='--', label='Instant Savings (W)')

ax2.legend(loc='upper right', framealpha=0.9)

# Draw vertical dashed marker lines and annotations for the bottom plot
for marker in phase_markers:
    ax2.axvline(x=marker, color='#333333', linestyle='-.', linewidth=1.5, alpha=0.8)

for x_pos, text in phase_labels:
    ax2.text(x_pos, 0.92, text, ha='center', va='top', transform=ax2.get_xaxis_transform(),
             bbox=dict(facecolor='white', alpha=0.85, edgecolor='#cccccc', boxstyle='round,pad=0.4'),
             fontweight='bold', color='#222222', fontsize=10)

# Set final X-axis limits to span 0 to exactly 120s (or max time if it slightly overshot)
plt.xlim(0, max(120.0, df['Time(s)'].max()))
plt.tight_layout(rect=[0, 0, 1, 0.95])

# 6. Save as high-resolution PNG
output_filename = 'lighting_power_efficiency_analysis.png'
plt.savefig(output_filename, dpi=300, bbox_inches='tight')
print(f"Power analysis plot successfully generated and saved as '{output_filename}' at 300 DPI.")
