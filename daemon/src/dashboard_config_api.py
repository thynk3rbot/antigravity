"""
Dashboard Configuration API — Manage field definitions for STATUS vs VSTATUS display.

Endpoints:
  GET  /api/config/fields/{device_class}/{status_type}  — Get field definitions
  GET  /api/config/fields/{device_class}                — Get all fields for class (status + vstatus)
  PUT  /api/config/fields/{device_class}/{status_type}  — Update field definitions
  GET  /api/config/all                                  — Get entire configuration
  POST /api/config/reset/{device_class}                 — Reset to defaults

Used by:
  - Dashboard (webapp): Fetch fields to render device cards and detail views
  - Configurator (webapp UI): Edit which metrics to show for each device class
"""

from fastapi import APIRouter, HTTPException, Body
from pydantic import BaseModel, Field
from typing import List, Dict, Any, Optional
import logging

try:
    from .field_config_manager import get_field_config_manager
except ImportError:
    from field_config_manager import get_field_config_manager

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/config", tags=["dashboard-config"])
config_manager = get_field_config_manager()


class FieldDefinition(BaseModel):
    """Single field definition."""
    key: str = Field(..., description="Field key/identifier")
    label: str = Field(..., description="Display label")
    type: str = Field(..., description="Data type (string, percent, dbm, duration, etc.)")
    visible: bool = Field(True, description="Whether to show by default")
    order: int = Field(..., description="Display order (lower = earlier)")
    critical: Optional[bool] = Field(False, description="Critical metric (may show alert if missing)")


class FieldUpdate(BaseModel):
    """Update request for field definitions."""
    fields: List[FieldDefinition] = Field(..., description="New field definitions")


class FieldConfig(BaseModel):
    """Complete field configuration for a class/type."""
    display_name: str
    fields: List[Dict[str, Any]]


@router.get(
    "/fields/{device_class}/{status_type}",
    response_model=List[FieldDefinition],
    summary="Get visible field definitions"
)
async def get_fields(device_class: str, status_type: str):
    """
    Get visible field definitions for a device class and status type.

    - **device_class**: v3 or v4
    - **status_type**: status (overview) or vstatus (detailed)

    Returns fields sorted by display order, visible fields only.
    """
    try:
        fields = config_manager.get_fields(device_class, status_type)
        return [FieldDefinition(**f) for f in fields]
    except Exception as e:
        logger.error(f"Failed to get fields: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get(
    "/fields/{device_class}/all",
    response_model=Dict[str, List[FieldDefinition]],
    summary="Get all field definitions for device class"
)
async def get_all_fields_for_class(device_class: str):
    """
    Get all field definitions for a device class (both status and vstatus).

    Includes both visible and hidden fields. Used by configurator UI.
    """
    try:
        status_fields = config_manager.get_all_fields(device_class, "status")
        vstatus_fields = config_manager.get_all_fields(device_class, "vstatus")

        return {
            "status": [FieldDefinition(**f) for f in status_fields],
            "vstatus": [FieldDefinition(**f) for f in vstatus_fields]
        }
    except Exception as e:
        logger.error(f"Failed to get all fields: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.put(
    "/fields/{device_class}/{status_type}",
    response_model=Dict[str, str],
    summary="Update field definitions"
)
async def update_fields(
    device_class: str,
    status_type: str,
    update: FieldUpdate = Body(...)
):
    """
    Update field definitions for a device class and status type.

    Changes are persisted to disk immediately and take effect for new queries.
    """
    try:
        field_dicts = [f.dict() for f in update.fields]
        success = config_manager.update_fields(device_class, status_type, field_dicts)

        if not success:
            raise HTTPException(
                status_code=400,
                detail=f"Failed to update fields for {device_class}/{status_type}"
            )

        return {
            "status": "ok",
            "device_class": device_class,
            "status_type": status_type
        }
    except Exception as e:
        logger.error(f"Failed to update fields: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get(
    "/all",
    response_model=Dict[str, Any],
    summary="Get entire configuration"
)
async def get_entire_config():
    """
    Get complete field configuration (all device classes, all status types).

    Used by configurator UI for bulk exports/imports.
    """
    try:
        config = config_manager.get_config()
        return config
    except Exception as e:
        logger.error(f"Failed to get config: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.post(
    "/reset/{device_class}",
    response_model=Dict[str, str],
    summary="Reset field definitions to defaults"
)
async def reset_to_defaults(device_class: Optional[str] = None):
    """
    Reset field definitions to defaults for a device class (or all if not specified).

    Useful if custom configuration gets corrupted.
    """
    try:
        success = config_manager.reset_to_defaults(device_class)

        if not success:
            raise HTTPException(status_code=500, detail="Failed to reset")

        return {
            "status": "ok",
            "reset": device_class or "all"
        }
    except Exception as e:
        logger.error(f"Failed to reset: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.get(
    "/health",
    response_model=Dict[str, str],
    summary="Health check for config service"
)
async def health_check():
    """Check if field config service is operational."""
    try:
        config = config_manager.get_config()
        return {
            "status": "healthy",
            "version": config.get("version", "unknown"),
            "last_updated": config.get("last_updated", "unknown")
        }
    except Exception as e:
        logger.error(f"Config health check failed: {e}")
        return {
            "status": "error",
            "error": str(e)
        }
