#!/bin/bash
set -euo pipefail

NAME=stylus-popup
VERSION=0.1
DIST=${DIST:-fc44}
RPMDIR=~/rpmbuild
REPODIR="$(cd "$(dirname "$0")/.." && pwd)"
SPEC=$REPODIR/packaging/$NAME.spec

usage() {
    cat <<EOF
Usage: $0 [clean|build|all]

  clean   Remove build artifacts and RPMs
  build   Build binary RPM only (src RPM if --srpm)
  all     Full cycle: clean + source + build [default]

Flags:
  --srpm    Also build source RPM
  --nodeps  Skip dependency install
EOF
    exit 1
}

# Parse flags
DOBUILD=true
DOCLEAN=false
DOSRPM=false
DODEPS=false
for arg; do
    case $arg in
        clean) DOCLEAN=true; DOBUILD=false ;;
        build) DOBUILD=true; DOCLEAN=false ;;
        all)   ;;
        --srpm)   DOSRPM=true ;;
        --nodeps) DODEPS=true ;;
        -h|--help) usage ;;
    esac
done

# Dependency install (run once)
install_deps() {
    if [[ $DODEPS == true ]]; then
        return
    fi
    if command -v dnf &>/dev/null; then
        sudo dnf builddep "$SPEC" 2>/dev/null || \
        sudo dnf install -y \
            cmake gcc-c++ qt6-qtbase-devel wayland-devel pkg-config \
            ninja-build rpm-build 2>/dev/null || true
    elif command -v zypper &>/dev/null; then
        sudo zypper install -y \
            cmake gcc-c++ patterns-devel-qt6-qtbase \
            wayland-devel pkg-config ninja rpm-build 2>/dev/null || true
    elif command -v apt &>/dev/null; then
        sudo apt install -y \
            cmake g++ qt6-base-dev libqt6waylandclient6-dev \
            pkg-config ninja-build rpm 2>/dev/null || true
    fi
}

# Setup rpmbuild dirs
setup_rpmdir() {
    mkdir -p $RPMDIR/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
    echo "Set up $RPMDIR"
}

# Create source tarball
make_source() {
    local tarball=$RPMDIR/SOURCES/$NAME-$VERSION.tar.gz
    git archive --format=tar HEAD --prefix=$NAME-$VERSION/ | gzip > $tarball
    cp "$REPODIR/packaging/$NAME.service" $RPMDIR/SOURCES/
    echo "Source: $tarball"
}

# Build binary (and optionally src) RPM
build_rpm() {
    local ba_args="-ba"
    [[ $DOSRPM == false ]] && ba_args="-bb"

    rpmbuild $ba_args \
        --define "_topdir $RPMDIR" \
        --define "dist .$DIST" \
        --define "project_version $VERSION" \
        --define "debug_package %{nil}" \
        $SPEC

    echo ""
    echo "=== Built RPMs ==="
    find $RPMDIR/RPMS -name "*.rpm" 2>/dev/null || true
    find $RPMDIR/SRPMS -name "*.rpm" 2>/dev/null || true
}

# Clean build artifacts
clean_all() {
    rm -rf $RPMDIR/{BUILD,BUILDROOT,RPMS,SRPMS}
    rm -f  $RPMDIR/SOURCES/$NAME-$VERSION.tar.gz
    rm -f  $RPMDIR/SOURCES/$NAME.service
    rm -rf build/
    echo "Cleaned."
}

# ── Main ──────────────────────────────────────────────────────────────────────
if [[ $DOCLEAN == true ]]; then
    clean_all
fi

if [[ $DOBUILD == true ]]; then
    install_deps
    setup_rpmdir
    make_source
    build_rpm
fi
