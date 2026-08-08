import logging

import requests
from fastapi import FastAPI, HTTPException

from app.models.trams import TramPositionsResponse
from app.services.departures import get_departures_for_stop
from app.services.trams import get_tram_positions


logger = logging.getLogger(__name__)

app = FastAPI(title="Tram display backend")

@app.get("/health")
def health():
    return {"status": "ok"}

@app.get("/display/{stop_id}")
def display(stop_id: str):
    try:
        return get_departures_for_stop(stop_id)
    except ValueError as e:
        raise HTTPException(status_code=404, detail=str(e))


@app.get(
    "/trams",
    response_model=TramPositionsResponse,
    response_model_exclude_none=True,
)
def trams():
    try:
        return get_tram_positions()
    except requests.RequestException as error:
        logger.warning("Realtime tram positions unavailable: %s", error)
        raise HTTPException(
            status_code=502,
            detail="Realtime tram positions are unavailable",
        ) from error
