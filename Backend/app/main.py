from fastapi import FastAPI, HTTPException
from app.services.departures import get_departures_for_stop

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
