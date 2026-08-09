# Wireless EV Charger with Smart Parking System

An embedded master-slave system utilizing two ESP32 microcontrollers to coordinate inductive wireless power transfer and automated vehicle alignment verification.

## Core Architecture
* **Dual-MCU Communications:** Implemented a low-latency inter-device communication layer driven by the **ESP-NOW protocol** to coordinate real-time tracking between the vehicle and charging pad.
* **Power Electronics Integration:** Interfaced an **XKT-412 wireless power module** stepping down a 12V DC input rail to a stable 5V DC charging output.
* **Closed-Loop Safety Isolation:** Tracks live power metrics using an **INA219 current/voltage sensor** and a hardware BMS, executing instant relay isolation via firmware if current or voltage deviations occur.
* **Sensor Fusion Network:** Employs an array of ultrasonic and infrared sensors to check proper vehicle detection and precise inductive coil alignment before initiating power transfer.
* **IoT Telemetry Gateway:** Encapsulates dynamic system diagnostics, charging state tracking, and safety faults to push real-time updates to users via a custom **Telegram Bot API**.

## System Control Loop Logic
1. **Alignment Verification:** Continually polls the IR and ultrasonic sensor network to verify the vehicle is perfectly centered over the charging pad.
2. **Handshake Protocol:** Triggers a low-power packet handshake over ESP-NOW to check the transmitter's readiness state.
3. **Active Monitoring:** Continuously parses incoming data packets containing active voltage/current profiles. If critical thresholds are crossed, the receiver kills the main relay and flags a safe state.
