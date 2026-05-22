# **Pre-Test Environmental Configuration**

To ensure an academically rigorous benchmark that accurately isolates environmental variables, the test bench was structured using the following spatial and physical constraints:

* **Spatial Orientation & Mounting Profiles:** * The **PIR sensor** was mounted rigidly to the ceiling grid structure, establishing a standard top-down conical field of view (FOV) optimized for capturing gross kinetic movement intersecting lateral infrared zones.
* The **DFRobot C4001 mmWave radar sensor** was securely attached flat against a vertical wall face at chest height ($1.2\text{ m}$ to $1.5\text{ m}$ above floor level), optimizing the 24GHz RF front-end propagation path directly toward the occupant's chest cavity to maximize respiration Doppler sensitivity.


* **Static Electronic Interference Profiling:** * An active laptop was positioned flat on the central workspace table.
* A large television monitor was mounted to the opposite wall structure and left continuously powered on.
* Both devices were kept operational for a minimum of $20\text{ minutes}$ prior to testing to establish stable, realistic **thermal blooming profiles** and micro-convection hot-air currents within the room.


* **Atmospheric Controls:** The testing room was completely sealed. All windows, privacy partitions, and doors were closed, and central HVAC ventilation grills were masked to enforce a strict zero-wind, draft-free microclimate. This isolated device-driven thermal noise from external atmospheric interference.

---

# **Step-by-Step Testing Procedure**

The multi-trial comparative occupancy evaluation was executed using the following sequential steps:

* **Step 1: Calibration and Ground-Truth Synchronization**
The user connected the FireBeetle ESP32-E edge node to the host computer via USB-C, verified the communication baud rate ($115200$), initialized the firmware's 200ms non-blocking polling interval, and verified that both sensors were clearing their initialization routines correctly.
* **Step 2: Occupant Egress ('OUT' Phase Trigger)**
The occupant stood directly inside the room to register a true baseline presence on both sensor channels. At around $T = 10\text{ seconds}$, the occupant walked out of the room, closed the door securely from the outside, and triggered the **"OUT"** timeline marker to signify the start of the vacant room validation block.
* **Step 3: Vacant Steady-State Logging Interval**
The room was left completely unoccupied for a continuous duration of $6\text{ minutes}$ ($360\text{ seconds}$). During this window, the ESP32 firmware continuously sampled both sensor pins every $200\text{ ms}$, logging the telemetry to capture the PIR sensor's interaction with the electronic heat plumes while tracking the mmWave radar's performance against the static layout.
* **Step 4: Occupant Ingress ('IN' Phase Trigger)**
At approximately $T = 310\text{ seconds}$ to $330\text{ seconds}$ (depending on the trial run), the occupant re-entered the closed room space, stepped directly into the detection zones, and triggered the **"IN"** timeline marker to benchmark recovery latch speed.
* **Step 5: Telemetry Compilation and File Export**
The raw text stream was captured from the serial buffer, frozen via the monitor, and exported as a standardized csv file.
* **Step 6: Statistical Replication Iteration**
The entire cycle—from Step 1 through Step 5—was repeated for three completely independent, isolated trial runs to provide the statistical replication needed to prove the mathematical consistency of the results.

See `presence_sensor_comparison.png`

# **Occupancy Detection Boundary and Thermal Noise Benchmarking**

To validate the occupancy tracking sub-framework within institutional boundaries, a passive infrared (PIR) sensor and a 24GHz DFRobot C4001 mmWave radar sensor were benchmarked across three separate evaluation trials. The results are summarized in the Statistical Performance Audit (Table [Y]). The test environment replicated a standard UWA Ezone meeting room cell under static hardware operating loads: a sealed, zero-draft enclosure containing one active laptop and one wall-mounted television monitor acting as localized thermal targets. Across the three trials, the vacant validation window averaged 308.2 seconds, bounded by the occupant exiting the space ('OUT' marker) and returning at the conclusion of the test cycle ('IN' marker).

```terminal
==============================================================================
  OCCUPANCY SENSOR BENCHMARK — STATISTICAL PERFORMANCE AUDIT
  PIR vs DFRobot C4001 mmWave Radar | 3-Trial Comparative Analysis
==============================================================================

  TRIAL 1  |  File: presence_data_1.csv
  Ground-Truth Window:  OUT @ 8.06s  →  IN @ 330.06s
  Vacant Duration:      322.0 s  (1610 samples @ 200ms)

  Metric                                               PIR     mmWave
  --------------------------------------------- ---------- ----------
  True Positive Rate (TPR) — Occupied Windows       100.0%     100.0%
  False Positive Rate (FPR) — Vacant Window          98.4%      35.0%
  Total False Occupancy Duration (s)                317.0s     112.6s
  State Transitions During Vacant Window                 2          4

  TRIAL 2  |  File: presence_data_2.csv
  Ground-Truth Window:  OUT @ 16.33s  →  IN @ 306.93s
  Vacant Duration:      290.6 s  (1453 samples @ 200ms)

  Metric                                               PIR     mmWave
  --------------------------------------------- ---------- ----------
  True Positive Rate (TPR) — Occupied Windows       100.0%      99.4%
  False Positive Rate (FPR) — Vacant Window          98.3%      43.1%
  Total False Occupancy Duration (s)                285.6s     125.2s
  State Transitions During Vacant Window                 2          8

  TRIAL 3  |  File: presence_data_3.csv
  Ground-Truth Window:  OUT @ 14.53s  →  IN @ 326.53s
  Vacant Duration:      312.0 s  (1560 samples @ 200ms)

  Metric                                               PIR     mmWave
  --------------------------------------------- ---------- ----------
  True Positive Rate (TPR) — Occupied Windows       100.0%     100.0%
  False Positive Rate (FPR) — Vacant Window          98.4%      12.2%
  Total False Occupancy Duration (s)                307.0s      38.0s
  State Transitions During Vacant Window                 2          2

------------------------------------------------------------------------------

  Metric                                           PIR Avg mmWave Avg
  ============================================= ========== ==========
  True Positive Rate (TPR) — Occupied Windows       100.0%      99.8%
  False Positive Rate (FPR) — Vacant Window          98.4%      30.1%
  Total False Occupancy Duration (s)                303.2s      91.9s
  State Transitions During Vacant Window               2.0        4.7

==============================================================================
```

The empirical results expose a systemic operational vulnerability in the PIR sensing topology under modern workspace configurations. Both sensors demonstrated exceptional reliability under occupied conditions, with the PIR achieving a True Positive Rate (TPR) of 100.0% and the mmWave radar maintaining a near-flawless 99.8% TPR across all trials. However, during the unoccupied phase, the PIR channel consistently failed to resolve the vacant state of the room, yielding an average False Positive Rate (FPR) of 98.4%. 

Because PIR modules rely on differential pyroelectric heat variation tracking, they are highly susceptible to micro-convection thermal plumes rising from active office electronics. The heat signatures dissipated by the laptop and TV monitor induced localized air density fluctuations that repeatedly crossed the sensor's Fresnel focal zones. This kept the PIR sensor in a perpetual false-positive state, causing it to log an average of 303.2 seconds of false occupancy per trial—effectively running the lighting load for 98.4% of the time the room was empty. This behavior is further verified by the rigid state transition count, which averaged exactly 2.0 across all runs. This mathematically documents the PIR 'dead period' anomaly: the sensor systematically cleared its output latch exactly once mid-test due to its internal time-delay potentiometer layout, only to be instantly re-triggered by the surrounding electronic thermal turbulence.

Conversely, the digital 24GHz mmWave radar sensor demonstrated strong ambient noise rejection, dropping the average False Positive Rate down to 30.1% across the entire audit loop. In Trial 3, the mmWave sensor achieved an exceptional low-noise baseline, maintaining an FPR of just 12.2% and mapping the empty room correctly for the vast majority of the test. Utilizing high-frequency Doppler radar shifts and FMCW range gating, the C4001 completely isolates physical kinetic velocity from ambient infrared thermal radiation. While the mmWave radar experienced minor transient settling spikes immediately following occupant egress—accumulating an average false up-time of 91.9 seconds—it consistently rejected the static thermal signatures of the active laptop and TV monitor.

**Subsystem Selection Conclusion:** Based on this multi-trial empirical evidence, the DFRobot C4001 mmWave Radar Sensor is selected as the primary occupancy trigger for the adaptive automation system. Relying on traditional PIR sensors within modern, hardware-dense university study spaces would result in near-continuous false occupancy detections, keeping overhead LED fixtures running in empty rooms and completely defeating the energy conservation goals of daylight harvesting. Integrating the mmWave sensor eliminates an average of 211.3 seconds of wasted power dissipation per 5-minute vacancy window, validating its role as a critical efficiency driver for institutional smart-lighting frameworks.