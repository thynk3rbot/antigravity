#!/bin/bash

################################################################################
# Antigravity Firmware Version Manager
#
# Usage:
#   ./tools/version.sh check <version>     # Validate version format
#   ./tools/version.sh bump <type> <plat>  # Bump version (major/minor/point)
#   ./tools/version.sh current <plat>      # Show current version
#   ./tools/version.sh set <version>       # Set specific version
#   ./tools/version.sh validate-all        # Validate all platforms
#
# Format: MAJOR.MINOR.POINT-PLATFORM
# Example: 0.0.00-2
################################################################################

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
VERSION_FILE="${REPO_ROOT}/.version"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Valid platforms
VALID_PLATFORMS=("2" "3" "4")

################################################################################
# Helper Functions
################################################################################

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

log_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

# Validate version format: MAJOR.MINOR.POINT-PLATFORM
validate_version_format() {
    local version="$1"

    if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]{2}-[234]$ ]]; then
        return 1
    fi
    return 0
}

# Extract components from version
parse_version() {
    local version="$1"

    # Extract MAJOR.MINOR.POINT-PLATFORM
    local major=$(echo "$version" | cut -d. -f1)
    local minor=$(echo "$version" | cut -d. -f2)
    local point=$(echo "$version" | cut -d. -f3 | cut -d- -f1)
    local platform=$(echo "$version" | cut -d- -f2)

    echo "${major} ${minor} ${point} ${platform}"
}

# Validate platform ID
validate_platform() {
    local platform="$1"

    for valid_plat in "${VALID_PLATFORMS[@]}"; do
        if [[ "$platform" == "$valid_plat" ]]; then
            return 0
        fi
    done
    return 1
}

################################################################################
# Commands
################################################################################

cmd_check() {
    local version="$1"

    if [[ -z "$version" ]]; then
        log_error "Usage: version.sh check <version>"
        echo "Example: version.sh check 0.0.00-2"
        exit 1
    fi

    if validate_version_format "$version"; then
        read major minor point platform < <(parse_version "$version")
        if validate_platform "$platform"; then
            log_success "Version format valid: $version"
            echo "  Major:    $major"
            echo "  Minor:    $minor"
            echo "  Point:    $point"
            echo "  Platform: $platform"
            exit 0
        else
            log_error "Invalid platform: $platform (valid: 2, 3, 4)"
            exit 1
        fi
    else
        log_error "Invalid version format: $version"
        echo "Expected format: MAJOR.MINOR.POINT-PLATFORM"
        echo "Example: 0.0.00-2"
        echo ""
        echo "Platform codes:"
        echo "  2 = Platform 2"
        echo "  3 = Platform 3"
        echo "  4 = Platform 4"
        exit 1
    fi
}

cmd_current() {
    local platform="$1"

    if [[ -z "$platform" ]]; then
        log_error "Usage: version.sh current <platform>"
        echo "Platforms: 2, 3, 4"
        exit 1
    fi

    if ! validate_platform "$platform"; then
        log_error "Invalid platform: $platform"
        exit 1
    fi

    if [[ ! -f "$VERSION_FILE" ]]; then
        log_warning "No version file found. Initialize with: version.sh set 0.0.00-$platform"
        exit 1
    fi

    local current_version=$(grep "^[0-9].*-$platform$" "$VERSION_FILE" | tail -1)

    if [[ -z "$current_version" ]]; then
        log_warning "No version found for platform $platform"
        exit 1
    fi

    echo "$current_version"
    log_success "Current version for platform $platform: $current_version"
}

cmd_set() {
    local version="$1"

    if [[ -z "$version" ]]; then
        log_error "Usage: version.sh set <version>"
        echo "Example: version.sh set 0.0.00-2"
        exit 1
    fi

    if ! validate_version_format "$version"; then
        log_error "Invalid version format: $version"
        cmd_check "$version"  # Show error details
        exit 1
    fi

    read major minor point platform < <(parse_version "$version")

    if ! validate_platform "$platform"; then
        log_error "Invalid platform: $platform"
        exit 1
    fi

    # Initialize or update .version file
    if [[ ! -f "$VERSION_FILE" ]]; then
        log_info "Creating new version file: $VERSION_FILE"
        touch "$VERSION_FILE"
    fi

    # Check if this platform version already exists
    if grep -q "^[0-9].*-$platform$" "$VERSION_FILE" 2>/dev/null; then
        # Update existing
        sed -i.bak "/^[0-9].*-$platform$/d" "$VERSION_FILE"
        log_info "Updated existing version for platform $platform"
    else
        log_info "Adding new version for platform $platform"
    fi

    echo "$version" >> "$VERSION_FILE"
    log_success "Version set to: $version"

    # Show all versions
    echo ""
    echo "Current versions:"
    cat "$VERSION_FILE" | sort
}

cmd_bump() {
    local bump_type="$1"
    local platform="$2"

    if [[ -z "$bump_type" ]] || [[ -z "$platform" ]]; then
        log_error "Usage: version.sh bump <type> <platform>"
        echo "Type: major, minor, point"
        echo "Platform: 2, 3, 4"
        echo "Example: version.sh bump point 2"
        exit 1
    fi

    if ! validate_platform "$platform"; then
        log_error "Invalid platform: $platform"
        exit 1
    fi

    if [[ ! -f "$VERSION_FILE" ]]; then
        log_error "No version file found. Initialize with: version.sh set 0.0.00-$platform"
        exit 1
    fi

    local current=$(grep "^[0-9].*-$platform$" "$VERSION_FILE" | tail -1)

    if [[ -z "$current" ]]; then
        log_error "No version found for platform $platform"
        exit 1
    fi

    read major minor point pf < <(parse_version "$current")

    case "$bump_type" in
        major)
            major=$((major + 1))
            minor=0
            point=0
            ;;
        minor)
            minor=$((minor + 1))
            point=0
            ;;
        point)
            point=$((point + 1))
            if [[ $point -lt 10 ]]; then
                point="0$point"
            fi
            ;;
        *)
            log_error "Invalid bump type: $bump_type (major, minor, point)"
            exit 1
            ;;
    esac

    local new_version="${major}.${minor}.$(printf '%02d' "$point")-${platform}"

    log_info "Bumping version for platform $platform"
    echo "  Current: $current"
    echo "  New:     $new_version"

    cmd_set "$new_version"
}

cmd_validate_all() {
    log_info "Validating all versions in $VERSION_FILE..."

    if [[ ! -f "$VERSION_FILE" ]]; then
        log_warning "No version file found"
        exit 1
    fi

    local all_valid=true
    local line_num=0

    while IFS= read -r version; do
        line_num=$((line_num + 1))

        if [[ -z "$version" ]] || [[ "$version" =~ ^#.* ]]; then
            continue  # Skip empty lines and comments
        fi

        if validate_version_format "$version"; then
            read major minor point platform < <(parse_version "$version")
            if validate_platform "$platform"; then
                log_success "Line $line_num: $version"
            else
                log_error "Line $line_num: Invalid platform in $version"
                all_valid=false
            fi
        else
            log_error "Line $line_num: Invalid format: $version"
            all_valid=false
        fi
    done < "$VERSION_FILE"

    echo ""
    if [[ "$all_valid" == true ]]; then
        log_success "All versions are valid!"
        exit 0
    else
        log_error "Some versions are invalid. Fix and retry."
        exit 1
    fi
}

cmd_help() {
    cat << 'EOF'
Antigravity Firmware Version Manager

USAGE:
    version.sh <command> [options]

COMMANDS:
    check <version>         Validate version format
                           Example: version.sh check 0.0.05-2

    current <platform>      Show current version for platform
                           Example: version.sh current 2

    set <version>          Set/update version
                           Example: version.sh set 0.1.00-3

    bump <type> <platform> Bump version (major/minor/point)
                           Example: version.sh bump point 2

    validate-all           Validate all versions in .version file

    help                   Show this help message

VERSION FORMAT:
    MAJOR.MINOR.POINT-PLATFORM

    MAJOR      = Major version (0, 1, 2, ...)
    MINOR      = Minor version (0, 1, 2, ...)
    POINT      = Point release (00, 01, 02, ..., 99) - zero-padded
    PLATFORM   = Target platform (2, 3, or 4)

EXAMPLES:
    version.sh check 0.0.00-2       # Validate version format
    version.sh set 0.0.00-2         # Initialize version for platform 2
    version.sh bump point 2         # Increment point release
    version.sh bump minor 3         # Increment minor version
    version.sh current 2            # Show current version for platform 2
    version.sh validate-all         # Check all versions are valid

PLATFORMS:
    2 = Platform 2
    3 = Platform 3
    4 = Platform 4
EOF
}

################################################################################
# Main
################################################################################

main() {
    local command="${1:-help}"

    case "$command" in
        check)
            cmd_check "$2"
            ;;
        current)
            cmd_current "$2"
            ;;
        set)
            cmd_set "$2"
            ;;
        bump)
            cmd_bump "$2" "$3"
            ;;
        validate-all)
            cmd_validate_all
            ;;
        help|--help|-h)
            cmd_help
            ;;
        *)
            log_error "Unknown command: $command"
            echo ""
            cmd_help
            exit 1
            ;;
    esac
}

main "$@"
