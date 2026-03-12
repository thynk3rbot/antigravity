# Nightly Automated Testing & Reporting System

## Overview
A complete automation pipeline for long-term endurance testing and analytical reporting, integrated into the LoRaLink Webapp.

## Components

### 1. Endurance Engine (`overdrive.py`)
- **Location**: `tools/testing/overdrive.py`
- **Function**: Executes repeated test cycles against the local or remote API.
- **Data Output**: Saves raw JSON logs (`overdrive_[ts].json`) and generates a markdown action-item list (`TODO_[date].md`).

### 2. Analytics Dashboard (`generate_report.py`)
- **Location**: `tools/testing/generate_report.py`
- **Function**: Processes JSON logs into interactive HTML reports.
- **Visuals**: Uses Chart.js for Latency Trends and Cycle Reliability charts.

### 3. Background Service Manager (`start_bg_services.py`)
- **Location**: `tools/start_bg_services.py`
- **Function**: Manages Webapp, Website, Docs, and MQTT servers as silent background processes.
- **Shortcuts**: `Start_Everything_BG.bat` / `Stop_Everything_BG.bat`.

### 4. Webapp Integration
- **Dev/QA Page**: `tools/webapp/static/devqa.html`
- **Features**: 
    - Trigger Overdrive tests with custom cycles/delays.
    - Browse and open historical reports directly from the UI.
- **Endpoints**:
    - `POST /api/test/overdrive`: Background execution.
    - `GET /api/test/reports`: Historical file index.
    - `GET /api/test/reports/{filename}`: Serve HTML content.

## Maintenance
- **Housekeeping**: Handled by Antigravity (Daily git commits and log rotation).
- **Log Location**: `tools/testing/logs/`
- **Report Location**: `tools/testing/reports/`
