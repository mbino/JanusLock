"""Minimal client for the Janus / Exitec cloud API (api.janus-lock.com)."""
from __future__ import annotations

import logging
from typing import Any

import aiohttp

_LOGGER = logging.getLogger(__name__)

BASE_URL = "https://api.janus-lock.com"
CLIENT_ID = "SmartLockV2"
CLIENT_SECRET = "de0a5c792d1dbec62b9d9dd44a4096d1616c1d820c9735aecb8b9fed85ad00d1"


class JanusAuthError(Exception):
    """Raised when authentication fails."""


class JanusApiError(Exception):
    """Raised when an API call fails."""


class JanusApi:
    """Async client that logs in and calls the Janus endpoints the app uses."""

    def __init__(self, session: aiohttp.ClientSession, username: str, password: str) -> None:
        self._session = session
        self._username = username
        self._password = password
        self._access_token: str | None = None
        self._refresh_token: str | None = None

    @property
    def access_token(self) -> str | None:
        return self._access_token

    async def login(self) -> dict[str, Any]:
        """Password-grant login; stores the access/refresh tokens."""
        data = {
            "username": self._username,
            "password": self._password,
            "grant_type": "password",
            "client_id": CLIENT_ID,
            "client_secret": CLIENT_SECRET,
        }
        async with self._session.post(f"{BASE_URL}/api/v1/oauth/token", data=data) as resp:
            body = await resp.json(content_type=None)
            if resp.status != 200 or "access_token" not in body:
                raise JanusAuthError(f"login failed ({resp.status}): {body}")
            self._access_token = body["access_token"]
            self._refresh_token = body.get("refresh_token")
            return body

    async def refresh(self) -> None:
        """Refresh the access token using the stored refresh token."""
        if not self._refresh_token:
            await self.login()
            return
        data = {
            "refresh_token": self._refresh_token,
            "grant_type": "refresh_token",
            "client_id": CLIENT_ID,
            "client_secret": CLIENT_SECRET,
        }
        async with self._session.post(f"{BASE_URL}/api/v1/oauth/token", data=data) as resp:
            body = await resp.json(content_type=None)
            if resp.status != 200 or "access_token" not in body:
                # refresh token expired -> full login
                await self.login()
                return
            self._access_token = body["access_token"]
            self._refresh_token = body.get("refresh_token", self._refresh_token)

    def _auth_headers(self) -> dict[str, str]:
        return {"Authorization": f"Bearer {self._access_token}"}

    async def _get(self, path: str, params: dict | None = None, _retry: bool = True) -> Any:
        async with self._session.get(
            f"{BASE_URL}{path}", params=params, headers=self._auth_headers()
        ) as resp:
            if resp.status in (401, 403) and _retry:
                await self.refresh()
                return await self._get(path, params, _retry=False)
            body = await resp.json(content_type=None)
            if resp.status != 200:
                raise JanusApiError(f"GET {path} failed ({resp.status}): {body}")
            return body

    async def _post(self, path: str, json: dict | None = None, data: dict | None = None,
                    _retry: bool = True) -> Any:
        async with self._session.post(
            f"{BASE_URL}{path}", json=json, data=data, headers=self._auth_headers()
        ) as resp:
            if resp.status in (401, 403) and _retry:
                await self.refresh()
                return await self._post(path, json, data, _retry=False)
            body = await resp.json(content_type=None)
            if resp.status not in (200, 201):
                raise JanusApiError(f"POST {path} failed ({resp.status}): {body}")
            return body

    async def get_profile(self) -> dict[str, Any]:
        """Full profile incl. locks, masterToken (tokenId 1) and tokens."""
        return await self._get("/api/v1/user/profile")

    async def get_day_code(self, lock_id: str, date_yyyymmdd: str) -> str:
        """One-day (offline TOTP) passcode for the given date (YYYYMMDD)."""
        body = await self._get(f"/api/v1/lock/{lock_id}/totp", params={"date": date_yyyymmdd})
        return body["totp"]

    async def add_token(self, payload: dict[str, Any]) -> dict[str, Any]:
        """Create a passcode/fingerprint access right; returns the token incl. tokenRaw."""
        return await self._post("/api/v1/token/add", json=payload)

    async def remove_token(self, lock_id: str, token_id: int) -> Any:
        return await self._post("/api/v1/token/remove", data={"lockId": lock_id, "tokenId": token_id})

    async def confirm_passcode_synced(self, lock_id: str, token_id: int) -> Any:
        return await self._post(
            "/api/v1/token/confirm-passcode-synced", data={"lockId": lock_id, "tokenId": token_id}
        )

    async def upload_history(
        self, lock_id: str, histories: list, id_start: int, id_end: int
    ) -> Any:
        """Upload unlock-history entries read from the lock. The server reconciles tokens
        (decrements/removes used one-time codes) from this."""
        return await self._post(
            f"/api/v1/lock/{lock_id}/unlock-history",
            json={
                "histories": histories,
                "unlockHistoryIdStart": id_start,
                "unlockHistoryIdEnd": id_end,
            },
        )

    async def download_history(self, lock_id: str) -> Any:
        """The unlock history already stored server-side (for display)."""
        return await self._get(f"/api/v1/lock/{lock_id}/unlock-history")
