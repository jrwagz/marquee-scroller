from fastapi import Depends, HTTPException, Request

from app.config import settings


def get_token_from_header(request: Request) -> str | None:
    auth = request.headers.get("Authorization", "")
    if auth.startswith("token "):
        return auth[6:]
    if auth.startswith("Bearer "):
        return auth[7:]
    return None


def require_auth(request: Request) -> str:
    token = get_token_from_header(request)
    if not settings.wagfam_api_key:
        raise HTTPException(status_code=500, detail="Server API key not configured")
    if token != settings.wagfam_api_key:
        raise HTTPException(status_code=401, detail="Invalid or missing token")
    return token


def optional_auth(request: Request) -> str | None:
    return get_token_from_header(request)
