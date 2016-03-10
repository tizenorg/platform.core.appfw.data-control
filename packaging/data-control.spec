Name:       data-control
Summary:    Data Control library
Version:    0.0.16
Release:    1
Group:      Application Framework/Libraries
License:    Apache-2.0
Source0:    %{name}-%{version}.tar.gz
BuildRequires:  cmake
BuildRequires:  pkgconfig(dlog)
BuildRequires:  pkgconfig(bundle)
BuildRequires:  pkgconfig(appsvc)
BuildRequires:  pkgconfig(pkgmgr-info)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(capi-base-common)
BuildRequires:  pkgconfig(cynara-client)
BuildRequires:  pkgconfig(sqlite3)
BuildRequires:  pkgconfig(openssl)

# runtime requires
Requires(post): /sbin/ldconfig
Requires(post): coreutils
Requires(postun): /sbin/ldconfig

Provides:   capi-data-control

%description
Data Control library

%package devel
Summary:  Data Control library (Development)
Group:    Application Framework/Development
Requires: %{name} = %{version}-%{release}

%description devel
Data Control library (DEV)


%prep
%setup -q

%build

MAJORVER=`echo %{version} | awk 'BEGIN {FS="."}{print $1}'`
%cmake . -DFULLVER=%{version} -DMAJORVER=${MAJORVER}

# Call make instruction with smp support
%__make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}

%make_install
mkdir -p %{buildroot}/tmp/datacontrol/request
mkdir -p %{buildroot}/tmp/datacontrol/result
mkdir -p %{buildroot}/usr/share/license
install LICENSE.APLv2  %{buildroot}/usr/share/license/%{name}

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%{_libdir}/lib%{name}.so.*
%{_libdir}/libcapi-data-control.so.*
%config %{_sysconfdir}/dbus-1/session.d/data-control.conf

%manifest %{name}.manifest
/usr/share/license/%{name}

%files devel
%{_includedir}/appfw/*.h
%{_libdir}/pkgconfig/*.pc
%{_libdir}/lib%{name}.so

