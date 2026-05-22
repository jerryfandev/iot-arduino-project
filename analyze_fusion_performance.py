import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

# Load the data
filename = "multimodal_occupancy.csv"
print(f"Loading data from {filename}...")

# Read lines manually to handle any lingering annotations, though there shouldn't be any
timestamps = []
radar_state = []
mic_trigger = []
fused_state = []

with open(filename, 'r', encoding='utf-8') as f:
    header = f.readline()
    for line in f:
        line = line.strip()
        if not line:
            continue
        
        # Clean up any annotations just in case
        if ' - OUT' in line:
            line = line.replace(' - OUT', '')
        elif ' - IN' in line:
            line = line.replace(' - IN', '')
            
        parts = line.split(',')
        if len(parts) >= 4:
            timestamps.append(float(parts[0]))
            radar_state.append(int(parts[1]))
            mic_trigger.append(int(parts[2]))
            fused_state.append(int(parts[3]))

df = pd.DataFrame({
    'Time': timestamps,
    'Radar': radar_state,
    'Mic': mic_trigger,
    'Fused': fused_state
})

# Plot the entire dataset
df_plot = df.copy()
max_plot_time = df['Time'].max() if not df.empty else 120

# ---------------------------------------------------------
# Performance Metrics Calculation (100% Sit-Tight Study)
# ---------------------------------------------------------
print("\n" + "="*60)
print("  MULTIMODAL FUSION PERFORMANCE ANALYSIS")
print("  Condition: 100% Occupied (Stationary Study)")
print("="*60)

total_samples = len(df)

if total_samples > 0:
    radar_fn = len(df[df['Radar'] == 0])
    fused_fn = len(df[df['Fused'] == 0])
    
    radar_fnr = (radar_fn / total_samples) * 100
    fused_fnr = (fused_fn / total_samples) * 100
    
    print("\n--- False Negative Rate (FNR) ---")
    print(f"Total Samples Analyzed: {total_samples}")
    print(f"Total Duration        : {max_plot_time:.1f} seconds")
    print("-" * 40)
    print(f"Raw Radar FNR : {radar_fnr:.1f}% ({radar_fn} false negatives)")
    print(f"Fused Sys FNR : {fused_fnr:.1f}% ({fused_fn} false negatives)")
    
    if fused_fnr == 0:
        print("\nConclusion: 0% FNR achieved due to total room coverage!")
else:
    print("Error: No data found in the dataset.")

print("="*60 + "\n")

# ---------------------------------------------------------
# Plotting
# ---------------------------------------------------------
fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(14, 8), sharex=True)

# Panel 1: Radar
ax1.step(df_plot['Time'], df_plot['Radar'], where='post', color='blue', linewidth=2)
ax1.set_ylabel('Radar State\n(0 or 1)', fontsize=12, fontweight='bold')
ax1.set_ylim(-0.2, 1.2)
ax1.set_yticks([0, 1])
ax1.grid(True, linestyle='--', alpha=0.5)

# Panel 2: Mic
ax2.step(df_plot['Time'], df_plot['Mic'], where='post', color='orange', linewidth=2)
ax2.set_ylabel('Mic Trigger\n(0 or 1)', fontsize=12, fontweight='bold')
ax2.set_ylim(-0.2, 1.2)
ax2.set_yticks([0, 1])
ax2.grid(True, linestyle='--', alpha=0.5)

# Panel 3: Fused System
ax3.step(df_plot['Time'], df_plot['Fused'], where='post', color='green', linewidth=2)
ax3.fill_between(df_plot['Time'], df_plot['Fused'], step='post', color='green', alpha=0.2)
ax3.set_ylabel('Fused System\n(0 or 1)', fontsize=12, fontweight='bold')
ax3.set_xlabel('Time (seconds)', fontsize=12, fontweight='bold')
ax3.set_ylim(-0.2, 1.2)
ax3.set_yticks([0, 1])
ax3.grid(True, linestyle='--', alpha=0.5)

# Annotate Top Panel Zone for 100% Occupied
ax1.text(max_plot_time / 2, 1.3, '100% Occupied (Stationary Study)', color='black', fontsize=12, fontweight='bold', ha='center', va='bottom')

plt.xlim(0, max_plot_time)
plt.suptitle('Multimodal Fusion Performance Analysis (Radar + Mic)', fontsize=16, fontweight='bold', y=0.95)
plt.tight_layout()

# Adjust top margin to fit text annotations
plt.subplots_adjust(top=0.88)

output_img = "multimodal_fusion_performance_analysis_stationary.png"
plt.savefig(output_img, dpi=300, bbox_inches='tight')
print(f"Plot successfully saved to: {output_img}")
