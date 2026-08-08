import pandas as pd
from datetime import datetime, timedelta
from app.config import GTFS_STATIC_PATH

# Load GTFS static CSV files into DataFrames at import time.
# These are read once when the application starts and kept in memory.
stops = pd.read_csv(GTFS_STATIC_PATH / "stops.txt", dtype={"stop_id": str})
routes = pd.read_csv(GTFS_STATIC_PATH / "routes.txt")
trips = pd.read_csv(GTFS_STATIC_PATH / "trips.txt")

# The realtime feed sometimes uses a different version prefix in trip_id. 
# This strips the prefix and keep the suffix so realtime and static trips can be matched.
trips["trip_id_suffix"] = trips["trip_id"].str.split("_", n=1).str[1]

# Some RT feeds only provide the final numeric trip number (no underscores).
# Keep just the last segment so we can match those too.
trips["trip_id_last"] = trips["trip_id"].str.rsplit("_", n=1).str[-1]

stop_times = pd.read_csv(GTFS_STATIC_PATH / "stop_times.txt", dtype={"stop_id": str})
calendar = pd.read_csv(
    GTFS_STATIC_PATH / "calendar.txt",
    dtype={"start_date": str, "end_date": str},
)
calendar_dates = pd.read_csv(
    GTFS_STATIC_PATH / "calendar_dates.txt",
    dtype={"date": str},
)

route_short_name_map = dict(zip(
    routes["route_id"].astype(str),
    routes["route_short_name"].astype(str),
))

# Column names in calendar.txt that indicate which days a service runs
_DOW_COLS = ["monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"]


def find_trip(trip_id):
    """Match a realtime trip ID to its static GTFS row."""
    if not trip_id:
        return None

    trip_id = str(trip_id)
    matching_trips = trips[trips["trip_id"] == trip_id]

    if matching_trips.empty and "_" in trip_id:
        trip_suffix = trip_id.split("_", 1)[1]
        matching_trips = trips[trips["trip_id_suffix"] == trip_suffix]

    if matching_trips.empty:
        matching_trips = trips[trips["trip_id_last"] == trip_id]

    return None if matching_trips.empty else matching_trips.iloc[0]


def _active_service_ids(date: datetime) -> set:
    """
    Determine which service_ids are running on a given date by checking
    the regular weekly schedule (calendar.txt) and then applying any
    one-off additions or removals from calendar_dates.txt.
    """
    date_str = date.strftime("%Y%m%d")
    dow = date.weekday() 

    # Regular weekly services whose date range covers today
    mask = (
        (calendar["start_date"] <= date_str)
        & (calendar["end_date"] >= date_str)
        & (calendar[_DOW_COLS[dow]] == 1)
    )
    active = set(calendar.loc[mask, "service_id"])

    # calendar_dates can add (type 1) or remove (type 2) services on specific dates
    exceptions = calendar_dates[calendar_dates["date"] == date_str]
    added = set(exceptions.loc[exceptions["exception_type"] == 1, "service_id"])
    removed = set(exceptions.loc[exceptions["exception_type"] == 2, "service_id"])

    return (active | added) - removed


def get_scheduled_departures(stop_id: str, now: datetime) -> list[dict]:
    """
    Look up static (timetable-based) departures for a stop that haven't
    happened yet. Used as a fallback for trips not present in the
    realtime feed.

    Returns a list of dicts:
        {trip_id_suffix, route_id, destination, departure_unix}
    """
    active_services = _active_service_ids(now)
    if not active_services:
        return []

    active_trips = trips[trips["service_id"].isin(active_services)]
    if active_trips.empty:
        return []

    # Join stop_times for the requested stop with today's active trips
    st = stop_times[stop_times["stop_id"] == stop_id].merge(
        active_trips[["trip_id", "trip_id_suffix", "route_id", "trip_headsign"]],
        on="trip_id",
        how="inner",
    )
    if st.empty:
        return []

    # GTFS departure_time is HH:MM:SS and may exceed 24:00:00 for
    # trips that run past midnight, so we add a timedelta to midnight.
    today_midnight = now.replace(hour=0, minute=0, second=0, microsecond=0)
    results = []

    for _, row in st.iterrows():
        h, m, s = (int(x) for x in str(row["departure_time"]).split(":"))
        dep_dt = today_midnight + timedelta(hours=h, minutes=m, seconds=s)

        if dep_dt <= now:
            continue

        results.append({
            "trip_id_suffix": row["trip_id_suffix"],
            "route_id": str(row["route_id"]),
            "destination": row["trip_headsign"],
            "departure_unix": int(dep_dt.timestamp()),
        })

    results.sort(key=lambda x: x["departure_unix"])
    return results
