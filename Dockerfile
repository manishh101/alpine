FROM alpine:3.21

# Copy custom repositories from the case study
COPY etc/apk/repositories /etc/apk/repositories

# Disable defunct Bintray repository and install dependencies
# We need nodejs for the file transfer server and bash for the run script
RUN sed -i 's/dl.bintray.com/#dl.bintray.com/g' /etc/apk/repositories && \
    apk update && \
    apk add --no-cache nodejs npm bash

# Copy the root filesystem contents to the container
COPY root /root

# Set the working directory to the root of the customized setup
WORKDIR /root

# The repository contains 'http_server_modules', which is a node_modules folder.
# We'll rename it to 'node_modules' so npm/node can find it properly.
RUN mv http_server_modules node_modules && \
    chmod +x Desktop/http-server.sh

# Add the local node_modules bin to PATH so 'http-server' can be found
ENV PATH="/root/node_modules/.bin:${PATH}"

# Expose port 8080 (the default for http-server)
EXPOSE 8080

# Define labels for metadata
LABEL description="Alpine Linux OS Case Study - Docker Environment"
LABEL author="5ujan"

# Start the file sharing server on container launch
CMD ["bash", "Desktop/http-server.sh"]
