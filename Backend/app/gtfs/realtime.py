import time
import logging
from dataclasses import dataclass, field
from threading import Lock

import requests
from google.protobuf.message import DecodeError
from google.transit import gtfs_realtime_pb2
from app.config import (
    GTFS_RT_CACHE_PATH,
    GTFS_RT_CACHE_TTL,
    GTFS_RT_URL,
    GTFS_RT_TIMEOUT,
    GTFS_RT_TOKEN,
    GTFS_RT_VEHICLE_POSITIONS_CACHE_PATH,
    GTFS_RT_VEHICLE_POSITIONS_URL,
)


logger = logging.getLogger(__name__)


@dataclass
class _FeedState:
    feed: object | None = None
    cached_at: float = 0.0
    lock: Lock = field(default_factory=Lock)


_trip_update_state = _FeedState()
_vehicle_position_state = _FeedState()


def _parse_feed(content: bytes):
    feed = gtfs_realtime_pb2.FeedMessage()
    feed.ParseFromString(content)
    return feed


def _load_disk_cache(cache_path):
    try:
        return _parse_feed(cache_path.read_bytes())
    except FileNotFoundError:
        return None
    except (OSError, DecodeError) as error:
        logger.warning("Could not load realtime feed cache: %s", error)
        return None


def _store_disk_cache(cache_path, content: bytes) -> None:
    try:
        cache_path.parent.mkdir(parents=True, exist_ok=True)
        temporary_path = cache_path.with_suffix(".tmp")
        temporary_path.write_bytes(content)
        temporary_path.replace(cache_path)
    except OSError as error:
        logger.warning("Could not persist realtime feed cache: %s", error)


def _get_feed(url, cache_path, state: _FeedState):
    """Return a feed cached long enough to respect Waltti's rate limit."""
    with state.lock:
        if state.feed is None:
            state.feed = _load_disk_cache(cache_path)

        now = time.monotonic()
        if state.feed is not None and now - state.cached_at < GTFS_RT_CACHE_TTL:
            return state.feed

        headers = {"Authorization": f"Basic {GTFS_RT_TOKEN}"}
        try:
            response = requests.get(
                url,
                headers=headers,
                timeout=GTFS_RT_TIMEOUT,
            )
            response.raise_for_status()
            feed = _parse_feed(response.content)
        except (requests.RequestException, DecodeError) as error:
            if state.feed is not None:
                logger.warning("Realtime feed refresh failed; using cached feed")
                state.cached_at = now
                return state.feed
            if isinstance(error, DecodeError):
                raise requests.RequestException("Invalid realtime feed") from error
            raise

        _store_disk_cache(cache_path, response.content)
        state.feed = feed
        state.cached_at = now
        return feed


def _get_trip_update_feed():
    return _get_feed(GTFS_RT_URL, GTFS_RT_CACHE_PATH, _trip_update_state)


def fetch_vehicle_positions_feed():
    """Fetch and decode the Waltti GTFS-Realtime vehicle-position feed."""
    return _get_feed(
        GTFS_RT_VEHICLE_POSITIONS_URL,
        GTFS_RT_VEHICLE_POSITIONS_CACHE_PATH,
        _vehicle_position_state,
    )


def fetch_departures_for_stop(stop_id: str) -> list:
    """
    Fetch the GTFS Realtime protobuf feed and extract upcoming departures
    for a specific stop. Skips canceled trips and skipped stops.

    Returns a list of dicts with keys:
        route_id, trip_id, trip_direction, destination,
        destination_stop_id, departure_time (unix), canceled
    """
    feed = _get_trip_update_feed()

    departures = []
    for entity in feed.entity:
        if not entity.HasField("trip_update"):
            continue

        trip = entity.trip_update.trip
        route_id = trip.route_id if trip.HasField("route_id") else None
        trip_id = trip.trip_id if trip.HasField("trip_id") else None
        trip_direction = trip.direction_id if trip.HasField("direction_id") else None
        destination = (
            entity.trip_update.vehicle.label
            if entity.trip_update.HasField("vehicle")
            and entity.trip_update.vehicle.HasField("label")
            else None
        )
        destination_stop_id = next((
            stu.stop_id
            for stu in reversed(entity.trip_update.stop_time_update)
            if stu.stop_id
        ), None)
        canceled_trip = (
            trip.schedule_relationship == gtfs_realtime_pb2.TripDescriptor.CANCELED
        )

        # Each trip update contains a list of stop time updates;
        # we only care about the one matching our stop.
        for stu in entity.trip_update.stop_time_update:
            if stu.stop_id != stop_id:
                continue

            canceled_stop = (
                stu.schedule_relationship
                == gtfs_realtime_pb2.TripUpdate.StopTimeUpdate.SKIPPED
            )

            departure_time = None
            if stu.HasField("departure") and stu.departure.HasField("time"):
                departure_time = stu.departure.time

            # Only include non-canceled departures with a known time
            if departure_time and not (canceled_trip or canceled_stop):
                departures.append({
                    "route_id": route_id,
                    "trip_id": trip_id,
                    "trip_direction": trip_direction,
                    "destination": destination,
                    "destination_stop_id": destination_stop_id,
                    "departure_time": departure_time,
                    "canceled": False,
                })

    return departures
