# LoRaLink Fleet Notes & Status

## Version Management
*   **Current Stable:** v0.2.8
*   **V4 Environment:** Fully updated (172.16.0.28, 172.16.0.29).
*   **V3 Environment:** Fully updated (172.16.0.26, 172.16.0.27). GPS enabled on UART1 (47/48).

*   **V2 Environment:** **BEHIND VERSION (v0.2.7)**. OTA timeout on 172.16.0.30.

## Hardware Observations
### Heltec WiFi LoRa 32 V2
*   **Display Quality:** Reported as "mottled, not sharp or defined". 
*   **Screen Specs:** Uses the same 0.96" 128x64 SSD1306 as V3, but requires careful power management.
*   **Potential Root Cause:**
    *   `VEXT` (GPIO 21) stability or PWM interference.
    *   Excessive Contrast (currently set to 255).
    *   Charge pump voltage fluctuations.
*   **Recommendation:** Reduce contrast to 128 or check VEXT pulse timing on next update.

## V2 Migration (Refactoring)
*   Phase 1 Complete (March 16).
*   Core HAL/Transport/App layers written for V2, V3, V4.
*   Encryption and NVS integration pending for migration finalization.
