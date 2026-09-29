"""Today's 1-day (offline TOTP) passcode as a sensor, per lock."""
from __future__ import annotations

from homeassistant.components.sensor import SensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN


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
        passcodes = []
        for tok in data.get("tokens", []):
            if not tok.get("passcode") and not tok.get("passcodeActivated"):
                # skip master token (tokenId 1) which has no passcode
                if tok.get("tokenId") == 1:
                    continue
            info = tok.get("info", {})
            passcodes.append(
                {
                    "token_id": tok.get("tokenId"),
                    "passcode": tok.get("passcode"),
                    "one_time": info.get("remainingUnlockCount") == 1,
                }
            )
        return {"date": data.get("day_date"), "passcodes": passcodes}
