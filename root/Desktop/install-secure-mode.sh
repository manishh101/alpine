#!/bin/sh
# install-secure-mode.sh
# Downloads the compiled BusyBox applet and configs into the Alpine VM

SERVER_IP=${1:-"10.0.2.2"}  # Default to VirtualBox host IP

echo "=========================================="
echo " Downloading setup-secure-mode payload... "
echo " Server IP: $SERVER_IP (Port: 8080)"
echo "=========================================="

# Create temp dir
mkdir -p /tmp/secure-payload
cd /tmp/secure-payload || exit 1

# Download files
echo "Downloading BusyBox binary..."
wget -q "http://$SERVER_IP:8080/Desktop/payload/busybox" -O busybox
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to download busybox. Is the file server running on $SERVER_IP:8080?"
    exit 1
fi

echo "Downloading config files..."
wget -q "http://$SERVER_IP:8080/Desktop/payload/nftables.nft" -O nftables.nft
wget -q "http://$SERVER_IP:8080/Desktop/payload/jail.local" -O jail.local

# Install busybox
echo "Installing new BusyBox binary..."
chmod +x busybox
mv busybox /bin/busybox

# Recreate symlinks
echo "Creating command symlinks..."
ln -sf /bin/busybox /usr/bin/sysinfoplus
ln -sf /bin/busybox /usr/bin/sysinfo+
ln -sf /bin/busybox /usr/sbin/setup-secure-mode

# Install configs
echo "Installing configuration files..."
mkdir -p /etc/nftables /etc/fail2ban
mv nftables.nft /etc/nftables/nftables.nft
mv jail.local /etc/fail2ban/jail.local

# Create needed directories
mkdir -p /var/backups/secure-mode /var/log

# Cleanup
cd /
rm -rf /tmp/secure-payload

echo "=========================================="
echo " Installation Complete! "
echo " You can now run: 'setup-secure-mode'"
echo "=========================================="
