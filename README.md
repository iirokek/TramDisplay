# Nysse Tram Schedule E-ink Display

Real-time tram departure display for Tampere public transit on an e-ink screen with battery power management. Firmware made for SeedStudio E1001 E-ink Display. 

## Backend Setup

**Requirements:**
- Python 3.13+ or Docker
- [Waltti API token](https://opendata.waltti.fi/) (Base64-encoded)
- GTFS static data files (download "GTFS_tampere.zip" from [ITS Factory database](https://data.itsfactory.fi/journeys/files/gtfs/latest/))

**Setup:**
1. Download GTFS static data and extract to `Backend/app/data/gtfs_static/`
2. Configure environment

**Docker:**
```bash
cd Backend
cp .env.example .env
# Edit .env and add your GTFS_RT_TOKEN
docker compose up -d
```

**Local:**
```bash
cd Backend
python -m venv venv
# On Windows
venv\Scripts\activate
# On Unix or macOS
source venv/bin/activate
pip install -r requirements.txt
cp .env.example .env
# Edit .env and add your GTFS_RT_TOKEN
uvicorn app.main:app --host 0.0.0.0 --port 8000 --reload
```

Backend runs at `http://localhost:8000`

## Firmware Setup

**Requirements:**
- SeedStudio E1001 or compatible ESP32 + e-ink display
- Arduino IDE or PlatformIO

**Installation:**
```bash
cd Firmware
cp config.h.example config.h
# Edit config.h with WiFi, API endpoint, and stop ID
# Upload main.ino to ESP32
```

Press button to wake device and display departures. Auto-sleeps after 5 minutes.
