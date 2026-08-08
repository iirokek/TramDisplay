from pydantic import BaseModel


class TramVehicle(BaseModel):
    """Memory-conscious vehicle data returned to the display."""

    vehicle_id: str | None = None
    route: str | None = None
    latitude: float
    longitude: float
    bearing: float | None = None


class TramPositionsResponse(BaseModel):
    updated_at: int
    vehicles: list[TramVehicle]
