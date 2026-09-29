"""The Janus Lock integration (cloud side: 1-day codes, passcode tokens)."""
from __future__ import annotations

import datetime
import logging
from datetime import timedelta

import voluptuous as vol

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, ServiceCall, SupportsResponse
from homeassistant.exceptions import ConfigEntryAuthFailed
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api import JanusApi, JanusApiError, JanusAuthError
from .const import CONF_PASSWORD, CONF_USERNAME, DOMAIN, UPDATE_INTERVAL_MINUTES

_LOGGER = logging.getLogger(__name__)
PLATFORMS = ["sensor"]


class JanusCoordinator(DataUpdateCoordinator):
    """Fetches the profile and today's 1-day code for each lock."""

    def __init__(self, hass: HomeAssistant, api: JanusApi) -> None:
        super().__init__(
            hass,
            _LOGGER,
            name=DOMAIN,
            update_interval=timedelta(minutes=UPDATE_INTERVAL_MINUTES),
        )
        self.api = api

    async def _async_update_data(self) -> dict:
        try:
            profile = await self.api.get_profile()
            today = datetime.date.today().strftime("%Y%m%d")
            locks: dict[str, dict] = {}
            for lock in profile.get("locks", []):
                lock_id = lock["_id"]
                info = lock.get("info", {})
                try:
                    day_code = await self.api.get_day_code(lock_id, today)
                except JanusApiError:
                    day_code = None
                locks[lock_id] = {
                    "info": info,
                    "name": info.get("alias") or info.get("name") or "Janus Lock",
                    "day_code": day_code,
                    "day_date": today,
                    "tokens": lock.get("tokens", []),
                }
            return locks
        except JanusAuthError as err:
            raise ConfigEntryAuthFailed(str(err)) from err
        except JanusApiError as err:
            raise UpdateFailed(str(err)) from err


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    session = async_get_clientsession(hass)
    api = JanusApi(session, entry.data[CONF_USERNAME], entry.data[CONF_PASSWORD])
    try:
        await api.login()
    except JanusAuthError as err:
        raise ConfigEntryAuthFailed(str(err)) from err

    coordinator = JanusCoordinator(hass, api)
    await coordinator.async_config_entry_first_refresh()

    hass.data.setdefault(DOMAIN, {})[entry.entry_id] = coordinator
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)

    _register_services(hass)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    unloaded = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unloaded:
        hass.data[DOMAIN].pop(entry.entry_id, None)
    return unloaded


def _first_coordinator(hass: HomeAssistant) -> JanusCoordinator | None:
    data = hass.data.get(DOMAIN, {})
    for value in data.values():
        if isinstance(value, JanusCoordinator):
            return value
    return None


def _register_services(hass: HomeAssistant) -> None:
    if hass.services.has_service(DOMAIN, "get_day_code"):
        return

    async def _get_day_code(call: ServiceCall):
        coord = _first_coordinator(hass)
        if coord is None:
            return {"error": "not configured"}
        lock_id = call.data.get("lock_id")
        if not lock_id:
            lock_id = next(iter(coord.data.keys()), None)
        date = call.data.get("date")
        if isinstance(date, datetime.date):
            date = date.strftime("%Y%m%d")
        if not date:
            date = datetime.date.today().strftime("%Y%m%d")
        code = await coord.api.get_day_code(lock_id, date)
        return {"lock_id": lock_id, "date": date, "code": code}

    hass.services.async_register(
        DOMAIN,
        "get_day_code",
        _get_day_code,
        schema=vol.Schema(
            {
                vol.Optional("lock_id"): cv.string,
                vol.Optional("date"): cv.string,
            }
        ),
        supports_response=SupportsResponse.ONLY,
    )
