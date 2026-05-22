import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import re

# ================================================================
# Configuration
# ================================================================
DATASETS = [
    "presence_data_1.csv",
    "presence_data_2.csv",
    "presence_data_3.csv",
]
OUTPUT_FILE = "presence_sensor_comparison.png"

# ================================================================
# Parser: Handles annotation tags like "1,1 - OUT" or "0,0 - IN"
# ================================================================
def load_annotated_csv(filepath):
    """
    Reads a presence CSV that may contain '- OUT' / '- IN' annotations.
    Returns a cleaned DataFrame and a dict of {timestamp: label}.
    """
    rows = []
    annotations = {}

    with open(filepath, 'r', encoding='utf-8') as f:
        for i, line in enumerate(f):
            line = line.strip()
            if not line or i == 0:  # Skip blank lines and the header
                continue

            # Check for annotation tag
            label = None
            if '- OUT' in line:
                label = 'OUT'
                line = line.replace('- OUT', '').strip().rstrip(',')
            elif '- IN' in line:
                label = 'IN'
                line = line.replace('- IN', '').strip().rstrip(',')

            # Parse the numeric fields
            parts = line.split(',')
            if len(parts) == 3:
                try:
                    ts  = float(parts[0])
                    pir = int(parts[1].strip())
                    mmw = int(parts[2].strip())
                    rows.append({'Timestamp': ts, 'PIR': pir, 'mmWave': mmw})
                    if label:
                        annotations[ts] = label
                except ValueError:
                    pass  # Skip malformed rows

    df = pd.DataFrame(rows)
    return df, annotations


# ================================================================
# Build Figure: 3 vertically stacked panels
# ================================================================
plt.rcParams.update({
    'font.size': 11,
    'axes.labelsize': 12,
    'axes.titlesize': 13,
    'legend.fontsize': 10,
    'font.family': 'sans-serif'
})

fig, axes = plt.subplots(nrows=3, ncols=1, figsize=(14, 10), sharex=False)
fig.suptitle('Presence Sensor Benchmark: PIR vs mmWave C4001\n(3-Trial Comparative Analysis)',
             fontweight='bold', fontsize=15, y=0.98)

grid_kw = dict(linestyle='--', linewidth=0.6, alpha=0.5, color='#aaaaaa')

for idx, (ax, filepath) in enumerate(zip(axes, DATASETS)):
    trial_label = f"Trial {idx + 1}"

    df, annotations = load_annotated_csv(filepath)

    if df.empty:
        ax.set_title(f"{trial_label} — No data found in {filepath}", color='red')
        continue

    t = df['Timestamp'].values

    # --- Step-plot both sensor channels ---
    ax.step(t, df['PIR'].values, where='post',
            color='#d62728', linewidth=1.8, label='PIR State', alpha=0.9)
    ax.step(t, df['mmWave'].values, where='post',
            color='#1f77b4', linewidth=1.8, label='mmWave State', alpha=0.9, linestyle='--')

    # --- Shade the absent period (between OUT and IN markers) ---
    out_time = None
    in_time  = None
    for ts, label in sorted(annotations.items()):
        if label == 'OUT':
            out_time = ts
        elif label == 'IN':
            in_time = ts

    if out_time is not None and in_time is not None:
        ax.axvspan(out_time, in_time, color='#d0e8ff', alpha=0.45, label='Absent Window')

    # --- Vertical annotation markers ---
    for ts, label in annotations.items():
        color = '#cc0000' if label == 'OUT' else '#007700'
        ax.axvline(x=ts, color=color, linestyle='-.', linewidth=1.8, alpha=0.9)
        ax.text(ts + (t[-1] * 0.01), 0.93, label,
                transform=ax.get_xaxis_transform(),
                color=color, fontsize=10, fontweight='bold',
                bbox=dict(facecolor='white', alpha=0.85, edgecolor=color,
                          boxstyle='round,pad=0.3'))

    # --- Axis formatting ---
    ax.set_title(f"{trial_label}  —  {filepath}", fontweight='bold')
    ax.set_ylabel('Binary State\n(0 = Absent / 1 = Present)', fontsize=10)
    ax.set_yticks([0, 1])
    ax.set_yticklabels(['0 (empty)', '1 (present)'])
    ax.set_ylim(-0.15, 1.25)
    ax.set_xlim(t[0], t[-1])
    ax.grid(True, **grid_kw)
    ax.legend(loc='lower right', framealpha=0.95)

    # --- Bottom axis label only on last panel ---
    if idx == len(DATASETS) - 1:
        ax.set_xlabel('Time (Seconds)', fontweight='bold')

plt.tight_layout(rect=[0, 0, 1, 0.96])
plt.savefig(OUTPUT_FILE, dpi=300, bbox_inches='tight')
print(f"\nPlot saved as '{OUTPUT_FILE}' at 300 DPI.")
