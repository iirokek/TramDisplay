import math
import time

from app.gtfs.realtime import fetch_vehicle_positions_feed
from app.gtfs.static import find_trip, route_short_name_map, routes
from app.models.trams import TramPositionsResponse, TramVehicle


TRAM_ROUTE_IDS = frozenset(
    routes.loc[routes["route_type"].astype(str) == "0", "route_id"].astype(str)
)


def _is_valid_coordinate(latitude: float, longitude: float) -> bool:
    return (
        math.isfinite(latitude)
        and math.isfinite(longitude)
        and -90.0 <= latitude <= 90.0
        and -180.0 <= longitude <= 180.0
    )


def _resolve_tram_route_id(vehicle) -> str | None:
    if vehicle.HasField("trip") and vehicle.trip.HasField("route_id"):
        route_id = vehicle.trip.route_id
        if route_id in TRAM_ROUTE_IDS:
            return route_id

    if vehicle.HasField("trip") and vehicle.trip.HasField("trip_id"):
        trip_row = find_trip(vehicle.trip.trip_id)
        if trip_row is not None:
            route_id = str(trip_row["route_id"])
            if route_id in TRAM_ROUTE_IDS:
                return route_id

    return None


def extract_tram_vehicles(feed) -> list[TramVehicle]:
    """Extract complete tram positions and ignore unrelated/malformed entities."""
    vehicles = []

    for entity in feed.entity:
        if not entity.HasField("vehicle"):
            continue

        vehicle = entity.vehicle
        route_id = _resolve_tram_route_id(vehicle)
        if route_id is None or not vehicle.HasField("position"):
            continue

        position = vehicle.position
        if not position.HasField("latitude") or not position.HasField("longitude"):
            continue

        latitude = float(position.latitude)
        longitude = float(position.longitude)
        if not _is_valid_coordinate(latitude, longitude):
            continue

        vehicle_id = None
        if vehicle.HasField("vehicle") and vehicle.vehicle.HasField("id"):
            vehicle_id = vehicle.vehicle.id or None

        bearing = None
        if position.HasField("bearing"):
            candidate_bearing = float(position.bearing)
            if math.isfinite(candidate_bearing) and 0.0 <= candidate_bearing < 360.0:
                bearing = candidate_bearing

        vehicles.append(TramVehicle(
            vehicle_id=vehicle_id,
            route=route_short_name_map.get(route_id, route_id),
            latitude=latitude,
            longitude=longitude,
            bearing=bearing,
        ))

    return vehicles


def get_tram_positions() -> TramPositionsResponse:
    feed = fetch_vehicle_positions_feed()
    updated_at = int(time.time())
    if feed.header.HasField("timestamp") and feed.header.timestamp > 0:
        updated_at = feed.header.timestamp

    return TramPositionsResponse(
        updated_at=updated_at,
        vehicles=extract_tram_vehicles(feed),
    )
