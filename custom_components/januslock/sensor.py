"""Today's 1-day (offline TOTP) passcode as a sensor, per lock."""
from __future__ import annotations

from homeassistant.components.sensor import SensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN

_DAYS = ["monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"]


def _describe(info: dict) -> dict:
    """Human-readable validity for a passcode token."""
    ruc = info.get("remainingUnlockCount")
    if ruc == 1:
        out: dict = {"type": "one-time"}
    elif (ruc or 0) >= 255:
        out = {"type": "permanent"}
    else:
        out = {"type": f"{ruc} uses"}
    time_from, time_to = info.get("timeValidFrom"), info.get("timeValidTo")
    if time_from or time_to:
        out["time"] = f"{time_from}-{time_to}"
    date_from, date_to = info.get("dateValidFrom"), info.get("dateValidTo")
    if date_from or date_to:
        out["date"] = f"{date_from}..{date_to}"
    weekday = info.get("weekday")
    if weekday and not all(weekday.get(d) for d in _DAYS):
        out["weekdays"] = [d for d in _DAYS if weekday.get(d)]
    return out


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities(
        JanusDayCodeSensor(coordinator, lock_id) for lock_id in coordinator.data
    )


class JanusDayCodeSensor(CoordinatorEntity, SensorEntity):
    """The passcode that works on the current day (server/offline TOTP)."""

    _attr_icon = "mdi:key-variant"
    _attr_has_entity_name = True

    def __init__(self, coordinator, lock_id: str) -> None:
        super().__init__(coordinator)
        self._lock_id = lock_id
        self._attr_unique_id = f"{lock_id}_day_code"
        self._attr_name = "Day code"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, lock_id)},
            name=coordinator.data[lock_id]["name"],
            manufacturer="Exitec / Janus",
            model=coordinator.data[lock_id]["info"].get("hardwareModel"),
        )

    @property
    def native_value(self):
        return self.coordinator.data.get(self._lock_id, {}).get("day_code")

    @property
    def extra_state_attributes(self):
        data = self.coordinator.data.get(self._lock_id, {})
        pins = []
        fingerprints = []
        for tok in data.get("tokens", []):
            token_id = tok.get("tokenId")
            if token_id == 1:
                continue  # master/admin token has no user code
            code = (tok.get("passcode") or "").strip()
            info = tok.get("info", {}) or {}
            if code.isdigit():
                pins.append({"token_id": token_id, "code": code, **_describe(info)})
            else:
                # a fingerprint token stores its label in the passcode field
                fingerprints.append({"token_id": token_id, "name": code or None})
        history = self.coordinator.history.get(self._lock_id, [])
        return {
            "date": data.get("day_date"),
            "pins": pins,
            "fingerprints": fingerprints,
            "recent_unlocks": history[-25:],
        }
