#!/bin/bash
set -e

echo "=========================================="
echo "Installing Cyrus IMAPD Dependencies"
echo "=========================================="

# Update package list
apt-get update

# Install basic build tools
echo "Installing build tools..."
apt-get install -y \
    autoconf \
    automake \
    autotools-dev \
    bash \
    bison \
    build-essential \
    ca-certificates \
    curl \
    flex \
    g++ \
    gcc \
    gdb \
    git \
    gperf \
    groff \
    less \
    libtool \
    make \
    pkg-config \
    rsync \
    sudo \
    transfig \
    valgrind \
    vim \
    wget

# Install Cyrus IMAPD library dependencies
echo "Installing Cyrus IMAPD libraries..."
apt-get install -y \
    libcunit1-dev \
    libdb-dev \
    libical-dev \
    libicu-dev \
    libjansson-dev \
    libldap2-dev \
    libnghttp2-dev \
    libopendkim-dev \
    libpcre2-dev \
    libsasl2-dev \
    libsqlite3-dev \
    libssl-dev \
    libxml2-dev \
    libxapian-dev \
    libzephyr-dev \
    uuid-dev \
    zlib1g-dev

# Install SASL packages
echo "Installing SASL packages..."
apt-get install -y \
    sasl2-bin \
    libsasl2-modules \
    libsasl2-modules-gssapi-mit \
    libsasl2-modules-ldap \
    libsasl2-modules-sql

# Install Perl and required modules
echo "Installing Perl and modules..."
apt-get install -y \
    perl \
    perl-doc \
    cpanminus \
    libanyevent-perl \
    libbsd-resource-perl \
    libclone-perl \
    libconfig-inifiles-perl \
    libdatetime-perl \
    libdatetime-format-iso8601-perl \
    libdbi-perl \
    libencode-perl \
    libfile-chdir-perl \
    libfile-libmagic-perl \
    libio-socket-inet6-perl \
    libio-stringy-perl \
    libjson-perl \
    libjson-xs-perl \
    libmail-imapclient-perl \
    libmime-types-perl \
    libmodule-install-perl \
    libnet-server-perl \
    libnews-nntpclient-perl \
    libstring-crc32-perl \
    libtest-unit-perl \
    libtext-levenshteinxs-perl \
    libtie-dxhash-perl \
    libtry-tiny-perl \
    libunix-syslog-perl \
    liburi-perl \
    libxml-generator-perl \
    libxml-libxml-perl \
    libyaml-libyaml-perl

# Install additional Perl modules via CPAN
echo "Installing additional Perl modules..."
cpanm --notest \
    Net::CalDAVTalk \
    Net::CardDAVTalk \
    Mail::JMAPTalk \
    Data::ICal \
    Text::VCardFast \
    XML::Spice \
    XML::Fast \
    Convert::ASN1 \
    Net::LDAP::Server::Test \
    List::Pairwise

# Install timezone data
echo "Installing timezone data..."
apt-get install -y tzdata

# Create Cyrus user and directories
echo "Setting up Cyrus user and directories..."
groupadd -g 1000 cyrus || true
useradd -u 1000 -g cyrus -d /srv/cyrus-imapd -s /bin/bash cyrus || true

# Create required directories
mkdir -p /tmp/cass
mkdir -p /usr/local/cyruslibs/share/cyrus-timezones
chmod 777 /tmp/cass

# Clean up
echo "Cleaning up..."
apt-get clean
rm -rf /var/lib/apt/lists/*

echo "=========================================="
echo "Dependencies installed successfully!"
echo "=========================================="

# Execute the command passed to the container
exec "$@"
