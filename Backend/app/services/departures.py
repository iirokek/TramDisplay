import time
from datetime import datetime
import logging
import requests
from app.gtfs.static import (
    stops,
    stop_times,
    trips,
    route_short_name_map,
    get_scheduled_departures,
)

logger = logging.getLogger(__name__)
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
    try:
        realtime_departures = fetch_departures_for_stop(stop_id)
    except requests.RequestException as error:
        logger.warning("Realtime departures unavailable; using static data: %s", error)
        realtime_departures = []
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
                trip_suffix = str(trip_id).split("_", 1)[1] if "_" in str(trip_id) else None
                if trip_suffix:
                    trips_row = trips[trips["trip_id_suffix"] == trip_suffix]

            if trips_row.empty:
                trips_row = trips[trips["trip_id_last"] == str(trip_id)]

            if not trips_row.empty:
                route_id_from_trip = str(trips_row.iloc[0]["route_id"])
                line = route_short_name_map.get(route_id_from_trip, route_id_from_trip)
                destination = trips_row.iloc[0]["trip_headsign"]
                suffix = trips_row.iloc[0]["trip_id_suffix"]
            else:
                logger.warning(
                    "RT trip not matched to static data - trip_id=%s route_id=%s",
                    trip_id, rt_dep["route_id"],
                )

        if not destination:
            destination = rt_dep["destination"]
        if not destination and rt_dep["destination_stop_id"]:
            destination_row = stops[
                stops["stop_id"] == str(rt_dep["destination_stop_id"])
            ]
            if not destination_row.empty:
                destination = destination_row.iloc[0]["stop_name"]

        # Current realtime trip IDs may not exist in an older static snapshot.
        # In that case, match the destination against trips serving this stop.
        if not line and destination:
            destination_name, separator, platform = destination.rpartition(" ")
            if not (separator and len(platform) == 1 and platform.isalpha()):
                destination_name = destination
            stop_trip_ids = stop_times.loc[
                stop_times["stop_id"] == stop_id, "trip_id"
            ]
            candidates = trips[
                trips["trip_id"].isin(stop_trip_ids)
                & trips["trip_headsign"].str.casefold().eq(destination_name.casefold())
            ]
            if not candidates.empty:
                route_id = str(candidates.iloc[0]["route_id"])
                line = route_short_name_map.get(route_id)
                destination = candidates.iloc[0]["trip_headsign"]

        if not line and rt_dep["route_id"]:
            line = route_short_name_map.get(str(rt_dep["route_id"]))

        if not line or not destination:
            continue

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

    # 3. Sort and deduplicate
    departures.sort(key=lambda x: x[0])
    seen_minutes: dict[str, int] = {}   
    deduped: list[tuple] = []

    for dep_unix, dep_tuple in departures:
        time_str = dep_tuple[2]  
        if time_str in seen_minutes:
            idx = seen_minutes[time_str]
            # Replace existing entry if the new one has a resolved destination
            if deduped[idx][1][1] == "Unknown" and dep_tuple[1] != "Unknown":
                deduped[idx] = (dep_unix, dep_tuple)
        else:
            seen_minutes[time_str] = len(deduped)
            deduped.append((dep_unix, dep_tuple))

    final = [dep for _, dep in deduped[:MAX_DEPARTURES]]

    return DisplayResponse(
        stop_name=stop_name,
        updated_at=int(time.time()),
        departures=final,
    )
