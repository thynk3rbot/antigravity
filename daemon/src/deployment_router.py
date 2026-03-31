"""
Deployment Router - Expose deployment config and feature flags to frontend
"""

from fastapi import APIRouter, HTTPException
from deployment_config import get_deployment_config

def get_deployment_router() -> APIRouter:
    """Create FastAPI router for deployment endpoints."""
    router = APIRouter(prefix="/api/deployment", tags=["deployment"])
    config = get_deployment_config()

    @router.get("/config")
    async def get_deployment_info():
        """Get current deployment configuration and feature flags."""
        return config.to_json()

    @router.get("/features")
    async def get_enabled_features():
        """Get list of enabled features."""
        return {
            "mode": config.mode,
            "enabled": config.get_enabled_features(),
            "disabled": [k for k, v in config.deployment.get("features", {}).items() if not v]
        }

    @router.get("/ui")
    async def get_ui_config():
        """Get UI configuration (theme, sidebar, panels)."""
        return {
            "sidebar": config.get_sidebar_level(),
            "panels": config.get_enabled_panels(),
            "theme": config.get_theme(),
            "default_view": config.get_default_view(),
        }

    @router.get("/access/{resource}")
    async def check_access(resource: str):
        """Check access level for a resource (registry, ota, diagnostics, mqtt)."""
        valid_resources = ["registry", "ota", "diagnostics", "mqtt"]
        if resource not in valid_resources:
            raise HTTPException(status_code=400, detail=f"Unknown resource: {resource}")

        access_level = config.deployment.get("api_access", {}).get(resource, "none")
        return {
            "resource": resource,
            "access_level": access_level,
            "can_read": access_level in ["full", "read-only"],
            "can_write": access_level == "full",
        }

    return router
