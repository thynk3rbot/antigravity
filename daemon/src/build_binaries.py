import PyInstaller.__main__
from monitor import monitor

def build_binaries():
    """
    Build standalone executables for Magic Cache.
    Generates: 
      - dist/lvc_service.exe
      - dist/transmitter.exe
    """
    monitor.log_info("Builder", "Starting Binary Build Process (PyInstaller)...")
    
    # 1. Build LVC Service
    monitor.log_info("Builder", "Building Magic LVC Service...")
    PyInstaller.__main__.run([
        'lvc_service.py',
        '--onefile',
        '--name=lvc_service',
        '--hidden-import=paho.mqtt.client',
        '--hidden-import=sqlalchemy',
        '--hidden-import=psycopg2',
        '--collect-all=sqlalchemy',
    ])

    # 2. Build Transmitter
    monitor.log_info("Builder", "Building Magic Transmitter...")
    PyInstaller.__main__.run([
        'transmitter.py',
        '--onefile',
        '--name=transmitter',
        '--hidden-import=paho.mqtt.client',
    ])

    # 3. Build Magic Console (TUI)
    monitor.log_info("Builder", "Building Magic Console TUI...")
    PyInstaller.__main__.run([
        'console.py',
        '--onefile',
        '--name=console',
        '--hidden-import=textual',
        '--hidden-import=rich',
    ])

    monitor.log_info("Builder", "Build Complete! Binaries are in the 'dist/' folder.")

if __name__ == "__main__":
    # Ensure we are in the magic_cache directory
    # If not, os.chdir(magic_cache_path)
    build_binaries()
