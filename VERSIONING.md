# Antigravity Versioning Methodology

## Version Format

All firmware releases follow this semantic versioning scheme with platform identifiers:

```
MAJOR.MINOR.POINT-PLATFORM
```

### Components

- **MAJOR**: Significant version changes or major feature releases
- **MINOR**: Feature additions and moderate changes
- **POINT**: Bug fixes, patches, and minor updates (zero-padded to 2 digits, e.g., `00`, `01`)
- **PLATFORM**: Target hardware platform identifier (2, 3, or 4)

### Platform Identifiers

| Platform | ID | Description |
|----------|----|----|
| Platform 2 | 2 | ESP32 Variant 2 |
| Platform 3 | 3 | ESP32 Variant 3 |
| Platform 4 | 4 | ESP32 Variant 4 |

## Examples

| Version | Meaning |
|---------|---------|
| `0.0.00-2` | Initial release for Platform 2 |
| `0.0.00-3` | Initial release for Platform 3 |
| `0.0.00-4` | Initial release for Platform 4 |
| `0.1.05-2` | Version 0.1.5 for Platform 2 |
| `1.2.03-3` | Version 1.2.3 for Platform 3 |
| `2.0.00-4` | Major release 2.0.0 for Platform 4 |

## Versioning Rules

### Required for Every Build

1. **Every firmware flash MUST include a version bump**
2. Version must be set BEFORE build/compilation
3. Version must be recorded in firmware binary
4. Version must be logged in release notes

### Incrementing Strategy

- **MAJOR**: Only when incompatible API/protocol changes
- **MINOR**: When adding features or significant updates
- **POINT**: For bug fixes and patches
  - Pad with leading zero: `00` → `01` → `05` → `10`
- **PLATFORM**: Never changes within a version, only build target

### Multi-Platform Releases

When releasing the same version to multiple platforms:

```
Release v1.0.05:
  - 1.0.05-2  (Platform 2)
  - 1.0.05-3  (Platform 3)
  - 1.0.05-4  (Platform 4)
```

All platforms get the same MAJOR.MINOR.POINT, only PLATFORM suffix differs.

## Implementation Guidelines

### In Firmware Code

Store version as a constant accessible at runtime:

```cpp
const char* FIRMWARE_VERSION = "0.0.00-2";  // Update for each build
const char* BUILD_DATE = __DATE__;
const char* BUILD_TIME = __TIME__;
```

### In Build Scripts

Version must be read and validated before compilation:

```bash
# Before building, validate version format
VERSION="${1:-0.0.00-2}"
if [[ ! $VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]{2}-[234]$ ]]; then
  echo "ERROR: Invalid version format: $VERSION"
  echo "Expected: MAJOR.MINOR.POINT-PLATFORM (e.g., 0.0.00-2)"
  exit 1
fi
```

## Enforcement

**ALL team members MUST follow this versioning system.**

- Pre-commit hooks validate version format
- Build system rejects invalid versions
- Release pipeline verifies version consistency
- Documentation must match firmware version

## Version History

Track all releases here:

| Version | Platform | Date | Description |
|---------|----------|------|-------------|
| 0.0.00-2 | 2 | 2026-03-19 | Initial firmware release |
| 0.0.00-3 | 3 | 2026-03-19 | Initial firmware release |
| 0.0.00-4 | 4 | 2026-03-19 | Initial firmware release |

---

**Last Updated**: 2026-03-19
**Maintained By**: Development Team
**Compliance Level**: MANDATORY
