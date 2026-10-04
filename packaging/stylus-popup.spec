%global project_version 0.1

Name:           stylus-popup
Version:        %{project_version}
Release:        1%{?dist}
Summary:        Material Design 3 stylus status popup for Wayland

License:        MIT
URL:            https://github.com/InioX/stylus-popup
Source0:        %{url}/archive/%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel
BuildRequires:  wayland-devel
BuildRequires:  pkg-config

Requires:         qt6-qtbase%{?_isa}
Requires:         libwayland-client
Requires(post):   systemd
Requires(preun):  systemd
Requires(postun): systemd

%description
Material Design 3 popup notification for the Xiaomi Nabu stylus wireless
charger, displaying battery level, charging status, and attachment state.

Target compositor is niri (Wayland), using the zwlr_layer_shell_v1 protocol
for top-of-screen overlay positioning.

The package ships a systemd *user* unit. The popup is a Wayland client that
draws into the session, reads the session user's config and runs the
configured button commands itself, so it belongs to the graphical session:
the user manager already provides WAYLAND_DISPLAY, NIRI_SOCKET, the session
user and the `input` group membership, and the unit starts and stops with
graphical-session.target.

Enable it per user, inside the session:

    systemctl --user enable --now %{name}.service

%prep
%autosetup -p1

%build
%cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_INSTALL_SYSCONFDIR=%{_sysconfdir} \
    -DCMAKE_INSTALL_LOCALSTATEDIR=%{_localstatedir} \
    -DSTYLUS_USER_UNIT_DIR=%{_userunitdir}

%ninja_build -C build

%install
%ninja_install -C build

%post
%systemd_user_post %{name}.service

%preun
%systemd_user_preun %{name}.service

%postun
%systemd_user_postun_with_restart %{name}.service

%files
%{_bindir}/%{name}
%{_userunitdir}/%{name}.service

%changelog
