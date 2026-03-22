# Local Process Simulator Environment Setup

To run the Local Process Simulator on your Windows machine, you need a Native C++ Toolchain (for `g++`) and the Python webapp dependencies.

## 1. C++ Toolchain (MinGW-w64 / MSYS2)
PlatformIO requires an external C++ compiler to build for the `native` environment on Windows. The easiest way is to install MSYS2 or MinGW-w64.

**Option A (Recommended): MSYS2**
1. Download the MSYS2 installer from [msys2.org](https://www.msys2.org/).
2. Run the installer and finish setup.
3. Open the "MSYS2 UCRT64" terminal and run:
   ```bash
   pacman -S mingw-w64-ucrt-x86_64-gcc
   ```
4. Add the `bin` directory to your System PATH: `C:\msys64\ucrt64\bin`.

**Option B: TDM-GCC**
1. Download from [jmeubank.github.io/tdm-gcc](https://jmeubank.github.io/tdm-gcc/).
2. Run the installer, making sure the "Add to PATH" option is checked.

## 2. Python Dependencies
The `server.py` and `simulator.py` require a few Python libraries (`aiohttp`, `fastapi`, etc.).
I created a `requirements.txt` file in `tools/webapp`. Run:

```powershell
cd c:\Code\antigravity\tools\webapp
pip install -r requirements.txt
```

## 3. Running the Simulator
Once the environment is set up:
1. Build the native binary: 
   ```powershell
   cd c:\Code\antigravity\firmware\v2
   pio run -e native
   ```
2. Start the webapp as usual (`server.py`).
3. Open the Cockpit dashboard (`http://localhost:8000`), navigate to the Prototype Hub, and click "Toggle Sim Mode."
