# ESP32 GPS SMS Tracker 🛰️📱

A robust, state-machine-driven GPS tracker built with an ESP32, SIM800L GSM module, and NEO-6M GPS. 

When you call the tracker, it immediately rejects the call, waits for a valid GPS fix, and replies with an SMS containing a Google Maps link to its current location.

## ✨ Features
* **Call-to-Locate:** Simply call the tracker to trigger an SMS location update. No data plan required, just standard SMS.
* **Non-Blocking Architecture:** Uses a centralized UART parser and a state machine to guarantee reliable SIM800L communication. It prevents the "race conditions" and dropped responses common when mixing GSM AT commands with continuous GPS polling.
* **Continuous GPS Tracking:** The NEO-6M is polled continuously in the background, ensuring the fastest possible location lock when requested.
* **Auto-Retry Logic:** If the SMS fails to send due to network instability, the system automatically retries.

---

## 🛠️ Hardware Requirements
* **Microcontroller:** ESP32 Node32S
* **GSM Module:** SIM800L (with an active SIM card)
* **GPS Module:** NEO-6M
* **Power Supply:** 12V
* **Power Regulation:** 2x LM2596 Buck Converters
* **Capacitor:** 1000µF (placed across the SIM800L power pins to handle 2A transmission spikes)

---

## 🔌 Wiring & Schematic

![Project Wiring Diagram](./diagram.png) 
*(Note: Replace `diagram.png` with the actual filename of your uploaded image in the repository.)*

### Pin Connections
| Component | Pin | ESP32 Pin | Notes |
| :--- | :--- | :--- | :--- |
| **NEO-6M GPS** | TX | `GPIO 16` (RX1) | |
| **NEO-6M GPS** | RX | `GPIO 17` (TX1) | |
| **SIM800L** | TX | `GPIO 27` (RX2) | |
| **SIM800L** | RX | `GPIO 26` (TX2) | |
| **SIM800L** | VCC | - | Powered by LM2596 tuned to **4.0V** |
| **ESP32** | VIN | - | Powered by LM2596 tuned to **5.0V** |

**⚠️ CRITICAL POWER WARNING:** The SIM800L requires a stable 3.4V - 4.4V power supply and can draw up to 2A in short bursts. **Do not power it directly from the ESP32.** Use the dedicated LM2596 step-down converter tuned to ~4.0V and ensure all components share a **Common Ground**.

---

## 💻 Software Architecture

Many beginner SIM800L projects fail because multiple functions try to read from the GSM module at the same time, leading to lost data (`RING`, `+CLIP`, `OK`). 

This project solves that by using a **Single Centralized Parser**. 
1. The ESP32 reads the SIM800L UART buffer in exactly one place.
2. It parses the data and sets global event flags.
3. A `switch/case` state machine handles the logic (`IDLE` -> `HANG_UP` -> `WAIT_GPS` -> `SEND_SMS`).
4. This ensures the ESP32 never "hangs" waiting for a response and can continually decode GPS data.

---

## 🚀 Installation & Setup

1. **Install the Arduino IDE.**
2. **Add ESP32 Board Support:** Go to `Tools > Board > Boards Manager` and install the `esp32` package.
3. **Install Dependencies:** Go to `Sketch > Include Library > Manage Libraries` and install:
   * `TinyGPSPlus` by Mikal Hart
4. **Clone this repository:**
   ```bash
   git clone [https://github.com/YOUR-USERNAME/YOUR-REPO-NAME.git](https://github.com/YOUR-USERNAME/YOUR-REPO-NAME.git)





1- Open the .ino file in the Arduino IDE.

2- Connect your ESP32 to your computer, select the correct COM port, and click Upload.

📖 Usage
1. Insert a micro-SIM card into the SIM800L (ensure SIM PIN lock is disabled).

2. Power up the system using the 12V source.

3. Wait approximately 1-2 minutes for the SIM800L to connect to the cellular network (the onboard LED will blink slowly: once every 3 seconds).

4. Call the phone number associated with the tracker's SIM card.

5. The tracker will reject your call and immediately text you a Google Maps link.

🐛 Troubleshooting
1. Module keeps restarting / LED blinks fast endlessly: The SIM800L isn't getting enough current. Check your 4.0V buck converter and ensure the 1000µF capacitor is wired closely to the module's VCC and GND.

2. GPS coordinates are 0.0000: The NEO-6M needs a clear view of the sky to get a fix. Take it outside. If the tracker can't get a fix within 10 seconds, it will send a fallback SMS stating the signal is unavailable.
