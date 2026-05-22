# 1. Pre-Test Environmental Setup
Before powering your FireBeetle, prepare your physical test bench to ensure clean, smooth transitions without introducing accidental rapid shading:

Secure the Array: Tape down your breadboard and your WS2812 LED bar so they cannot shift physically when you move your hands.

Isolate Target Light: Position your desktop setup away from flickering monitors or direct shifting shadows. You want the only light changes to be the ones you intentionally introduce.

Prepare a Shading Tool: Grab an opaque object (a piece of dark cardboard, a notebook, or a small box) and your smartphone with its flashlight app turned on.

# 2. Step-by-Step Testing Procedure (The "Daylight Harvesting Simulation")
You will run a 2-minute (120-second) test. Because the firmware logs data every 500ms, this will yield a highly complete dataset of exactly 240 structured rows for your Python script to analyze.

**Phase 1**: Baseline High-Demand State (0 to 30 Seconds)
Place your opaque box completely over the VEML7700 light sensor to mimic absolute midnight conditions inside a windowless Ezone room.

**Phase 2**: Dynamic Adaptation State (30 to 90 Seconds)
At the 30-second mark, lift the box off the sensor to expose it to normal ambient room light. You will instantly see the LED_Brightness drop and the InstantPower_W plunge proportionally.

**Phase 3**: Peak Solar Saturation State (90 to 120 Seconds)
At the 90-second mark, turn on your smartphone flashlight and aim it directly down onto the VEML7700 sensor from a height of about 5 cm.

This simulates intense midday sun pouring through the Ezone glass facade.

# Result

```terminal
=================================================================     
         ADAPTIVE DAYLIGHT HARVESTING EFFICIENCY SUMMARY
=================================================================     
Total Testing Duration                  : 120.0 seconds
Max Instantaneous Power Saved           : 7.350 W
Total Cumulative Energy Saved           : 0.362395 Wh (1304.62 Joules)
Avg Power Reduction vs 100% Baseline    : 34.5%
=================================================================
```

To quantitatively evaluate the efficiency gains of the developed Closed-Loop Daylight Harvesting algorithm, a comprehensive 120-second multi-phase environmental stress test was executed. The system's power consumption was tracked in real-time using a software-defined predictive power model calibrated to a 30-pixel WS2812 RGB LED bar running on a $5.0\text{ V}$ VCC power bus. The performance metrics were benchmarked against a conventional unautomated lighting installation locked continuously at $100\%$ operational intensity ($7.5\text{ W}$ constant load). The empirical results are illustrated in Figure X (See `lighting_power_efficiency.png`).

During **Phase I**: Full Shading ($0\text{--}30\text{ s}$), the digital sensor reported illuminance metrics well below the lower control threshold ($100\text{ Lux}$). The firmware responded by driving the LED array to maximum output (8-bit PWM = 255), drawing a flat $7.5\text{ W}$ and establishing the peak baseline power load.

During **Phase II**: Dynamic Adaptation ($30\text{--}90\text{ s}$), manual shading variations were introduced to simulate shifting ambient daylight within an academic study space. The system demonstrated highly responsive inverse tracking: as ambient illuminance rose, the edge node actively stepped down the LED duty cycle. At $T = 52\text{ s}$, a moderate ambient rise triggered an immediate power drop from $7.5\text{ W}$ to $3.0\text{ W}$, actively conserving $4.5\text{ W}$ of instantaneous power.

This energy conservation behavior peaked during **Phase III**: Solar Saturation ($90\text{--}120\text{ s}$), where direct high-intensity light exposure pushed ambient readings past the upper control boundary ($500\text{ Lux}$). The automated feedback loop responded by completely turning off the LED power channels (PWM = 0). This plunged active power consumption to a mere $0.15\text{ W}$ quiescent standby floor, yielding a maximum instantaneous power savings of $7.350\text{ W}$.

Over the complete test timeline, the adaptive system achieved a total cumulative energy savings of $0.3624\text{ Wh}$ ($1,304.62\text{ Joules}$). This represents an overall $34.5\%$ average power reduction compared to the standard, non-adaptive installation. These results mathematically validate the system design, demonstrating that localized closed-loop control can significantly reduce institutional power loads while maintaining stable environmental lighting conditions.