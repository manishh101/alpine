###############################################
# Stage 1: Build custom BusyBox with applets
#   - sysinfo+          (system information)
#   - setup-secure-mode (security hardening)
###############################################
FROM alpine:edge AS busybox-builder

# Install build dependencies
RUN apk add --no-cache build-base linux-headers perl bash wget

# Download BusyBox 1.37.0 source
RUN wget -q https://busybox.net/downloads/busybox-1.37.0.tar.bz2 && \
    tar xjf busybox-1.37.0.tar.bz2 && \
    rm busybox-1.37.0.tar.bz2

WORKDIR /busybox-1.37.0

# Copy our custom applets into the BusyBox source tree
COPY busybox-applets/sysinfoplus.c miscutils/sysinfoplus.c
COPY busybox-applets/setup_secure_mode.c miscutils/setup_secure_mode.c

# Regenerate build files so BusyBox picks up our applets' metadata,
# then configure and compile.
# We disable CONFIG_TC because it fails to build with newer Linux headers.
RUN make defconfig && \
    sed -i 's/^CONFIG_TC=y/# CONFIG_TC is not set/' .config && \
    make -j$(nproc)

###############################################
# Stage 2: Final Alpine image
###############################################
FROM alpine:edge

# Copy custom repositories from the case study
COPY etc/apk/repositories /etc/apk/repositories

# Disable defunct Bintray repository and install dependencies
# We need nodejs for the file transfer server and bash for the run script
# We also install security tool dependencies for setup-secure-mode
RUN sed -i '/dl.bintray.com/d' /etc/apk/repositories && \
    sed -i '/packages.sury.org/d' /etc/apk/repositories && \
    apk update && \
    apk add --no-cache nodejs npm bash \
        nftables openssh fail2ban openrc

# Replace the default BusyBox with our custom build
COPY --from=busybox-builder /busybox-1.37.0/busybox /bin/busybox

# Create applet symlinks (BusyBox multi-call style)
RUN ln -sf /bin/busybox /usr/bin/sysinfoplus && \
    ln -sf /bin/busybox /usr/bin/sysinfo+ && \
    ln -sf /bin/busybox /usr/sbin/setup-secure-mode

# Copy configuration files for security tools
COPY etc/nftables/nftables.nft /etc/nftables/nftables.nft
COPY etc/fail2ban/jail.local /etc/fail2ban/jail.local

# Create required directories for setup-secure-mode
RUN mkdir -p /var/backups/secure-mode /var/log

# Copy the root filesystem contents to the container
COPY root /root

# Prepare the payload directory for the VM to download the new files
RUN mkdir -p /root/Desktop/payload && \
    cp /bin/busybox /root/Desktop/payload/busybox && \
    cp /etc/nftables/nftables.nft /root/Desktop/payload/nftables.nft && \
    cp /etc/fail2ban/jail.local /root/Desktop/payload/jail.local

# Set the working directory to the root of the customized setup
WORKDIR /root

# We will install http-server globally to ensure it works properly across environments.
RUN npm install -g http-server && \
    chmod +x Desktop/http-server.sh && \
    chmod +x Desktop/install-secure-mode.sh

# Expose port 8080 (the default for http-server)
EXPOSE 8080

# Define labels for metadata
LABEL description="Alpine Linux OS Case Study - Docker Environment with Custom BusyBox"
LABEL author="5ujan"

# Start the file sharing server on container launch
CMD ["bash", "Desktop/http-server.sh"]
