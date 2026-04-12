%global project_version 0.1

Name:           stylus-popup
Version:        %{project_version}
Release:        1%{?dist}
Summary:        Material Design 3 stylus status popup for Wayland

License:        MIT
URL:            https://github.com/InioX/stylus-popup
Source0:        %{url}/archive/%{version}/%{name}-%{version}.tar.gz
Source1:        %{name}.service

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel
BuildRequires:  wayland-devel
BuildRequires:  pkg-config

Requires:       qt6-qtbase%{?_isa}
Requires:       libwayland-client

%description
Material Design 3 popup notification for the Xiaomi Nabu stylus wireless
charger, displaying battery level, charging status, and attachment state.

Target compositor is niri (Wayland), using the zwlr_layer_shell_v1 protocol
for top-of-screen overlay positioning.

%prep
%autosetup -p1

%build
%cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_INSTALL_SYSCONFDIR=%{_sysconfdir} \
    -DCMAKE_INSTALL_LOCALSTATEDIR=%{_localstatedir}

%ninja_build -C build

%install
%ninja_install -C build
install -Dm644 %{SOURCE1} %{buildroot}%{_userunitdir}/%{name}.service

%post
systemctl --user daemon-reload > /dev/null 2>&1 || true

%postun
systemctl --user daemon-reload > /dev/null 2>&1 || true

%files
%{_bindir}/%{name}
%{_userunitdir}/%{name}.service

%changelog
