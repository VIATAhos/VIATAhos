#!/bin/bash
# Generate floppy.img (FAT12)
dd if=/dev/zero of=floppy.img bs=512 count=2880
# Format as FAT12 (if mkfs.fat is available, else rely on some pre-built or hex payload)
# We can just leave it as zero for now, we'll need to figure out formatting later if needed

# Generate CD-ROM ISO
mkdir -p iso_root
echo "Hello CD" > iso_root/hello.txt
# hdiutil is available on Mac to make ISOs
hdiutil makehybrid -iso -joliet -o cdrom.iso iso_root/

# Generate TAR for Tape
mkdir -p tar_root
echo "Tape backup" > tar_root/backup.txt
tar -cvf tape.tar -C tar_root/ backup.txt
