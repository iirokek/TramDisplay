import requests
from google.transit import gtfs_realtime_pb2
from app.config import GTFS_RT_URL, GTFS_RT_TIMEOUT, GTFS_RT_TOKEN


def fetch_departures_for_stop(stop_id: str) -> list:
    """
    Fetch the GTFS Realtime protobuf feed and extract upcoming departures
    for a specific stop. Skips canceled trips and skipped stops.

    Returns a list of dicts with keys:
        route_id, trip_id, trip_direction, departure_time (unix), canceled
    """
    headers = {"Authorization": f"Basic {GTFS_RT_TOKEN}"}

    # Download and parse the protobuf feed
    feed = gtfs_realtime_pb2.FeedMessage()
    response = requests.get(GTFS_RT_URL, headers=headers, timeout=GTFS_RT_TIMEOUT)
    response.raise_for_status()
    feed.ParseFromString(response.content)

    departures = []
    for entity in feed.entity:
        if not entity.HasField("trip_update"):
            continue

        trip = entity.trip_update.trip
        route_id = trip.route_id if trip.HasField("route_id") else None
        trip_id = trip.trip_id if trip.HasField("trip_id") else None
        trip_direction = trip.direction_id if trip.HasField("direction_id") else None
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
                    "departure_time": departure_time,
                    "canceled": False,
                })

    return departures
