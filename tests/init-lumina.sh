# Step 1: Define paths for clarity (adjust if your libraries are elsewhere)
# Use 'ldd /bin/echo' to verify these paths on your system.
# The linker is often in /lib64 OR /lib/x86_64-linux-gnu
LINKER_PATH=$(ldd /bin/echo | grep 'ld-linux' | awk '{print $1}')
LIBC_PATH=$(ldd /bin/echo | grep 'libc.so' | awk '{print $3}')

# Step 2: Create the base directory structure with sudo
echo "Creating base directories in /var/lib/lumina..."
sudo mkdir -p /var/lib/lumina/runtimes/org.freedesktop.Platform/22.08/files/bin
sudo mkdir -p /var/lib/lumina/apps/io.lumina.HelloWorld/files

# Step 3: Populate the Runtime with shared libraries
echo "Populating the shared runtime..."
# Copy and set permissions for the executable
sudo cp /bin/echo /var/lib/lumina/runtimes/org.freedesktop.Platform/22.08/files/bin/echo
sudo chmod 755 /var/lib/lumina/runtimes/org.freedesktop.Platform/22.08/files/bin/echo

# Create the library directories inside the runtime
sudo mkdir -p "/var/lib/lumina/runtimes/org.freedesktop.Platform/22.08/files$(dirname ${LINKER_PATH})"
sudo mkdir -p "/var/lib/lumina/runtimes/org.freedesktop.Platform/22.08/files$(dirname ${LIBC_PATH})"

# Copy and set permissions for the dynamic linker and libc
sudo cp "${LINKER_PATH}" "/var/lib/lumina/runtimes/org.freedesktop.Platform/22.08/files${LINKER_PATH}"
sudo chmod 755 "/var/lib/lumina/runtimes/org.freedesktop.Platform/22.08/files${LINKER_PATH}"

sudo cp "${LIBC_PATH}" "/var/lib/lumina/runtimes/org.freedesktop.Platform/22.08/files${LIBC_PATH}"
sudo chmod 755 "/var/lib/lumina/runtimes/org.freedesktop.Platform/22.08/files${LIBC_PATH}"

# Step 4: Create the Application Manifest
echo "Creating the application manifest..."
# This file tells lumina what runtime the app needs and how to run it.
sudo bash -c 'cat > /var/lib/lumina/apps/io.lumina.HelloWorld/metadata.json' << EOF
{
  "id": "io.lumina.HelloWorld",
  "runtime": "org.freedesktop.Platform/22.08",
  "command": ["/bin/echo", "Success: Hello from a REAL Lumina container!"]
}
EOF

echo "Manual setup complete."