import time
from datetime import datetime
from app.gtfs.static import stops, trips, route_short_name_map, get_scheduled_departures
from app.gtfs.realtime import fetch_departures_for_stop
from app.models.display import DisplayResponse
from app.config import MAX_DEPARTURES


def _format_time(dt: datetime) -> str:
    """Format a datetime as H:MM f"""
    return f"{dt.hour}:{dt.minute:02d}"


def get_departures_for_stop(stop_id: str) -> DisplayResponse:
    """
    Build a complete departure board for a single stop by merging
    two data sources:

      1. GTFS Realtime — live vehicle positions / predictions (preferred)
      2. GTFS Static   — published timetable (fills in any gaps)

    Realtime entries take priority. Static entries are only included
    when no matching realtime record exists (matched by trip_id suffix).
    """
    now = datetime.now()

    # Resolve the stop name from static data
    stop_row = stops[stops["stop_id"] == stop_id]
    if stop_row.empty:
        raise ValueError("Unknown stop_id")
    stop_name = stop_row.iloc[0]["stop_name"]

    # 1. Realtime departures
    realtime_departures = fetch_departures_for_stop(stop_id)
    realtime_departures.sort(key=lambda x: x["departure_time"])

    # Track which trips we've already seen so we don't duplicate them
    # when we add static departures later.
    seen_suffixes: set[str] = set()
    departures: list[tuple[int, tuple]] = []

    for rt_dep in realtime_departures:
        if rt_dep["canceled"]:
            continue

        departure_dt = datetime.fromtimestamp(rt_dep["departure_time"])
        minutes = int((departure_dt - now).total_seconds() / 60)
        if minutes < 0:
            continue

        # Try to resolve the human-readable line number and destination
        # by matching the realtime trip_id back to the static schedule.
        trip_id = rt_dep["trip_id"]
        line = None
        destination = None
        suffix = None

        if trip_id:
            # First try an exact trip_id match
            trips_row = trips[trips["trip_id"] == str(trip_id)]

            # Fall back to suffix-based matching if the prefix differs
            if trips_row.empty:
                suffix = str(trip_id).split("_", 1)[1] if "_" in str(trip_id) else None
                if suffix:
                    trips_row = trips[trips["trip_id_suffix"] == suffix]

            if not trips_row.empty:
                route_id_from_trip = str(trips_row.iloc[0]["route_id"])
                line = route_short_name_map.get(route_id_from_trip, route_id_from_trip)
                destination = trips_row.iloc[0]["trip_headsign"]
                suffix = trips_row.iloc[0]["trip_id_suffix"]

        # If static lookup failed, use whatever the realtime feed provides
        route_id = rt_dep["route_id"]
        if not line:
            line = route_short_name_map.get(str(route_id), str(route_id)) if route_id else "???"
        if not destination:
            destination = "Unknown"

        if suffix:
            seen_suffixes.add(suffix)

        departures.append((
            rt_dep["departure_time"],
            (line, destination, _format_time(departure_dt), minutes),
        ))

    # 2. Static scheduled departures (fill in trips missing from RT)
    scheduled = get_scheduled_departures(stop_id, now)

    for sched in scheduled:
        if sched["trip_id_suffix"] in seen_suffixes:
            continue

        dep_unix = sched["departure_unix"]
        departure_dt = datetime.fromtimestamp(dep_unix)
        minutes = int((departure_dt - now).total_seconds() / 60)

        line = route_short_name_map.get(sched["route_id"], sched["route_id"])
        destination = sched["destination"] or "Unknown"

        departures.append((
            dep_unix,
            (line, destination, _format_time(departure_dt), minutes),
        ))

    # 3. Sort by departure time and return the nearest N departures
    departures.sort(key=lambda x: x[0])
    final = [dep for _, dep in departures[:MAX_DEPARTURES]]

    return DisplayResponse(
        stop_name=stop_name,
        updated_at=int(time.time()),
        departures=final,
    )