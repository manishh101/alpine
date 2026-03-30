FROM alpine:edge

# Copy custom repositories from the case study
COPY etc/apk/repositories /etc/apk/repositories

# Disable defunct Bintray repository and install dependencies
# We need nodejs for the file transfer server and bash for the run script
RUN sed -i '/dl.bintray.com/d' /etc/apk/repositories && \
    sed -i '/packages.sury.org/d' /etc/apk/repositories && \
    apk update && \
    apk add --no-cache nodejs npm bash

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
LABEL description="Alpine Linux OS Case Study - Docker Environment"
LABEL author="5ujan"

# Start the file sharing server on container launch
CMD ["bash", "Desktop/http-server.sh"]
