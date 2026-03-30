###############################################
# Stage 1: Build custom BusyBox with sysinfo+ applet
###############################################
FROM alpine:edge AS busybox-builder

# Install build dependencies
RUN apk add --no-cache build-base linux-headers perl bash wget

# Download BusyBox 1.37.0 source
RUN wget -q https://busybox.net/downloads/busybox-1.37.0.tar.bz2 && \
    tar xjf busybox-1.37.0.tar.bz2 && \
    rm busybox-1.37.0.tar.bz2

WORKDIR /busybox-1.37.0

# Copy our custom applet into the BusyBox source tree
COPY busybox-applets/sysinfoplus.c miscutils/sysinfoplus.c

# Regenerate build files so BusyBox picks up our applet's metadata,
# then configure and compile
RUN make defconfig && \
    make -j$(nproc)

###############################################
# Stage 2: Final Alpine image
###############################################
FROM alpine:edge

# Copy custom repositories from the case study
COPY etc/apk/repositories /etc/apk/repositories

# Disable defunct Bintray repository and install dependencies
# We need nodejs for the file transfer server and bash for the run script
RUN sed -i '/dl.bintray.com/d' /etc/apk/repositories && \
    sed -i '/packages.sury.org/d' /etc/apk/repositories && \
    apk update && \
    apk add --no-cache nodejs npm bash

# Replace the default BusyBox with our custom build
COPY --from=busybox-builder /busybox-1.37.0/busybox /bin/busybox

# Create the sysinfoplus symlink (BusyBox multi-call style)
RUN ln -sf /bin/busybox /usr/bin/sysinfoplus && \
    ln -sf /bin/busybox /usr/bin/sysinfo+

# Copy the root filesystem contents to the container
COPY root /root

# Set the working directory to the root of the customized setup
WORKDIR /root

# We will install http-server globally to ensure it works properly across environments.
RUN npm install -g http-server && \
    chmod +x Desktop/http-server.sh

# Expose port 8080 (the default for http-server)
EXPOSE 8080

# Define labels for metadata
LABEL description="Alpine Linux OS Case Study - Docker Environment with Custom BusyBox"
LABEL author="5ujan"

# Start the file sharing server on container launch
CMD ["bash", "Desktop/http-server.sh"]
