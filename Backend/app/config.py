import os
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent

# Path to the bundled GTFS static schedule files (CSV)
GTFS_STATIC_PATH = BASE_DIR / "app" / "data" / "gtfs_static"

# Waltti open data GTFS Realtime endpoint for Tampere (Nysse)
GTFS_RT_URL = os.environ.get(
    "GTFS_RT_URL",
    "https://data.waltti.fi/tampere/api/gtfsrealtime/v1.0/feed/tripupdate",
)
GTFS_RT_VEHICLE_POSITIONS_URL = os.environ.get(
    "GTFS_RT_VEHICLE_POSITIONS_URL",
    "https://data.waltti.fi/tampere/api/gtfsrealtime/v1.0/feed/vehicleposition",
)
GTFS_RT_TIMEOUT = int(os.environ.get("GTFS_RT_TIMEOUT", "5"))
GTFS_RT_CACHE_TTL = int(os.environ.get("GTFS_RT_CACHE_TTL", "30"))
GTFS_RT_CACHE_PATH = Path(os.environ.get(
    "GTFS_RT_CACHE_PATH",
    "/var/cache/tram-display/tripupdate.pb",
))
GTFS_RT_VEHICLE_POSITIONS_CACHE_PATH = Path(os.environ.get(
    "GTFS_RT_VEHICLE_POSITIONS_CACHE_PATH",
    "/var/cache/tram-display/vehicleposition.pb",
))

# Base64-encoded API credentials — must be set in the environment
GTFS_RT_TOKEN = os.environ["GTFS_RT_TOKEN"]

# How many departures to include in a single display response
MAX_DEPARTURES = int(os.environ.get("MAX_DEPARTURES", "6"))
