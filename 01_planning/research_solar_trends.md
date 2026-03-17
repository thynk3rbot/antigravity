---
status: planning
owner: claude
---

# Research: Solar Trend Accuracy Audit

## Objective
Evaluate the precision of the current `PowerManager` trend analysis and its sensitivity to solar charging events.

## Current Implementation Analysis
- **Sampling**: `PowerManager::Update` calls `recordSample` every 30s.
- **Storage**: Ring buffer of size 12 (6 minutes of history).
- **Algorithm**: `getVelocityMvMin` uses a simple two-point slope between the oldest and newest sample.

## Identified Risks
1. **Noise Sensitivity**: ESP32-S3 ADC noise (±20-50mV) can cause huge velocity swings when using a simple two-point slope over just 6 minutes.
2. **Short Window**: 6 minutes might be too short to detect slow solar charging (~2-5mV/min).
3. **Linear Regression Absence**: A simple slope doesn't account for volatility within the window.

## Requested Research
1. **Denoising**: Propose an exponential moving average (EMA) or median filter for raw ADC samples before recording.
2. **Better Slope**: Implement a least-squares linear regression over the 12 points.
3. **Power Mode Integration**: Should the sampling interval increase in `CRITICAL` mode to save more power?
4. **Time-to-Full (TTF)**: If `velocity > 0`, calculate an estimated time to reach 4.2V.

## Expected Output
A `spec.md` in `/01_planning/` detailing the New-and-Improved Solar Logic.
