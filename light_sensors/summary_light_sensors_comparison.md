# 1. Define testing conditions
Condition A: Ambient Diffusion (Baseline): Place the board in a room with steady, indirect natural daylight or standard overhead room lights. This establishes your baseline noise profile for both sensors under static conditions.

Condition B: High-Frequency Interruption (The "Flicker" Test): Position your setup near a window or under a desk lamp, and rapidly pass your hand or a piece of paper over the sensors. This simulates dynamic shadow casting from students walking past the Ezone's glass partitions.

Condition C: Saturation Limit (The "Sunlight" Test): Expose the sensors directly to a high-intensity light source, such as a phone flashlight held 5 cm away, or direct midday sunlight through glass. This tests the upper ceiling limits and clipping behavior of both the analog ADC and the digital photodiode.

# 2. Types of Tests to Conduct

**Test 1 - Execute Condition C (300 Seconds)**: Electrical Noise & Signal Stability Analysis
Objective: Determine which sensor provides a cleaner signal when the environment is completely still.

What you are looking for: Watch the Arduino Serial Plotter during Condition A. Analog sensors on solderless breadboards often suffer from high-frequency "ripple noise" due to electromagnetic interference (EMI) from the Wi-Fi chip or loose jumper wires. The digital VEML6030 should show a completely flat line because its signal conditioning happens internally on the silicon chip.

```terminal
=======================================================================
     LUMINANCE SENSOR BENCHMARK METRICS (5-Minute Baseline)
=======================================================================
Metric                    | Digital Lux (VEML6030) | Analog Voltage (V)
-----------------------------------------------------------------------
Mean                      |               384.5116 |            0.3312
Standard Dev (RMS Noise)  |                 0.7153 |            0.0088
Peak-to-Peak (Max-Min)    |                 3.1400 |            0.0780
Relative Std Dev (RSD %)  |                0.1860% |           2.6489%
=======================================================================
```

See `luminance_analysis_plots.png`

The two plots on the right-hand side illustrate the frequency distribution profiles of both sensor pathways over the 5-minute baseline interval. The digital VEML6030 pathway produces an exceptionally narrow, high-frequency distribution curve centered tightly around its mean baseline. This tight distribution profile visually confirms the sensor's high precision and low random error floor ($\text{RSD} = 0.1860\%$). In contrast, the analog sensor's profile exhibits a severely widened, low-amplitude distribution curve. This flat profile indicates a high variance in the data units, graphically exposing the constant baseline drift and noise fluctuations ($\text{RSD} = 2.6489\%$) induced by the unshielded hardware environment.

To establish a mathematical foundation for sensor selection within the environmental automation framework, a quantitative statistical analysis was performed on a 300-second steady-state dataset. Because the two candidate sensor channels operate on completely distinct physical units (Lux vs. Volts), the Relative Standard Deviation ($\text{RSD} = \frac{\sigma}{\mu} \times 100\%$) was utilized as a normalized metric to evaluate relative signal-to-noise ratios (SNR) equitably.

As compiled in the benchmark metrics, the digital VEML6030 demonstrates premium signal stability, maintaining a normalized noise floor of just 0.1860%. Its worst-case peak-to-peak variance ($\Delta x = 3.14$ Lux) represents a minor 0.82% deviation from the mean baseline ($\mu = 384.51$ Lux). This tight constraint validates the efficacy of the sensor’s localized silicon-level signal conditioning and native 16-bit digital integration.

In stark contrast, the raw analog sensor configuration exhibits a degraded noise profile. It demonstrates an RSD of 2.6489%, indicating a relative noise magnitude over 14.24 times greater than its digital competitor. Most critically, the analog channel records a peak-to-peak noise swing of 0.0780 V against a mean signal amplitude of only 0.3312 V. This reveals that random unshielded breadboard interference and power rail ripple manifest as extreme transient spikes constituting up to 23.55% of the total average signal scale.

If directly coupled to the internal closed-loop firmware algorithm, these high-amplitude analog errors would generate severe systemic oscillations in the 8-bit PWM duty cycle, inducing high-frequency visual flickering in the WS2812B RGB LED bar. Mitigating this would require dedicating valuable processor cycles at the edge node to execute multi-stage digital filtering math. Consequently, these statistical performance metrics provide empirical justification for selecting the digital I2C architecture to ensure steady, filter-free environmental regulation.

**Test 2 - Execute Condition B (60 Seconds)**: Dynamic Response & Sensitivity Mapping
Objective: Evaluate how quickly and smoothly each sensor registers sudden shifts in room brightness.

What you are looking for: During Condition B, observe which sensor responds faster and if the analog voltage tracking experiences a lag or erratic spiking compared to the VEML6030's 16-bit digital resolution. This proves whether the sensor is responsive enough to drive your sub-perceptual Closed-Loop LED Dimming without distracting students.

See `dynamic_sensor_tracking.png`

To stress-test the transient responsiveness of the dual-sensor array for real-world application in the UWA Ezone meeting rooms, a 60-second dynamic tracking test was conducted under varying tracking frequencies (Figure Y).

During the Low-Frequency Sweeps ($0\text{--}20\text{ s}$), both sensor topologies exhibit tight temporal alignment, successfully tracking baseline illuminance drops without measurable phase lag.However, during the High-Frequency Waves ($20\text{--}40\text{ s}$), a critical hardware trade-off emerges. While the raw analog interface reacts instantaneously to high-speed transitions, the digital VEML6030 experiences peak truncation, flattening out near its $376\text{ Lux}$ ceiling. This behavior is attributed to the digital sensor's internal silicon integration time combined with the firmware's $500\text{ ms}$ non-blocking polling interval, which approaches the Nyquist limits of high-frequency occupant motion.

In the final Transient Step Changes ($40\text{--}60\text{ s}$) sector, the digital sensor demonstrates its superior mitigation of transient noise, flatlining smoothly during holds, whereas the analog line continues to introduce high-frequency electrical jitter.

Design Conclusion: This dynamic analysis indicates that while the digital VEML6030 provides the clean, noise-free telemetry required to safely drive stable, sub-perceptual Closed-Loop LED Dimming, its polling rate must be optimized in software to ensure it does not lag during rapid human movement. Conversely, the analog sensor provides superior raw speed but remains too electrically volatile for filter-free feedback control.

**Test 3 - Execute Condition C (30 Seconds)**: Shine your phone light directly onto both sensors. Check if the analog input flatlines perfectly at 4095 (clipping the 12-bit ADC ceiling) and see how the VEML6030 handles maximum saturation.

See `saturation_test.png`

To stress-test the upper operational boundaries and saturation behavior of the prototyping array, a final 30-second direct-exposure saturation test was executed (Figure Z). This test simulates extreme environmental lighting anomalies, such as direct solar glare focused through the glass partitions of the UWA Ezone study spaces.

The empirical results expose a critical physical limitation within the raw analog channel. Under direct exposure, the analog sensor’s output voltage completely saturates the ESP32’s internal Successive Approximation Register (SAR) ADC1 block. The telemetry flatlines continuously at the absolute hardware limit of $4095$ counts—representing the data ceiling of a standard 12-bit resolution register ($2^{12} - 1$). At this threshold, the analog subsystem enters an operational dead-zone, clipping all input waves and losing the ability to track any localized lighting variations.Conversely, the digital VEML6030 sensor manages the high-intensity saturation event without signal degradation. Bypassing the host microcontroller's native ADC entirely, the VEML6030 utilizes its onboard 16-bit calculation architecture and programmable gain settings to smoothly resolve the exposure profile at approximately $2400\text{ Lux}$. The digital sensor maintains a fully functional, noise-immune curve, continuing to deliver reliable, granular environmental metrics over the I2C bus.

Final Architectural Hardware Justification: Compounding the data collected across all three testing conditions—Steady-State Noise Floors (Condition A), Dynamic Tracking Latencies (Condition B), and Saturation Boundaries (Condition C)—the Digital VEML6030 is officially selected over the raw analog sensor for the system's daylight harvesting infrastructure. While the analog sensor offers sub-millisecond edge response speeds, its vulnerability to unshielded breadboard ripple noise ($\text{RSD} = 3.5494\%$) and immediate bit-width register clipping under high illuminance levels makes it unreliable for automated feedback control. The VEML6030 provides superior electrical isolation, natural digital signal filtering, and a wide dynamic range, ensuring stable, sub-perceptual luminance regulation for the study environment.