from pydantic import BaseModel
from typing import List, Tuple

# Each departure is a tuple: (line number, destination, "H:MM", minutes until departure)
Departure = Tuple[str, str, str, int]


class DisplayResponse(BaseModel):
    """Payload returned to the e-ink display client."""
    stop_name: str
    updated_at: int
    departures: List[Departure]
