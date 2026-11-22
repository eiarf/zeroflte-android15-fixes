#!/sbin/sh
#
# Grow the system filesystem to the whole SYSTEM partition after the OTA
# wrote the image, which is smaller than the partition. Uses the
# recovery's own e2fsprogs: /system/bin in Lineage Recovery, /sbin in
# TWRP. Without them the system stays at image size and still boots.

dev=$1

for dir in /system/bin /sbin; do
	[ -z "$e2fsck" ] && [ -x "$dir/e2fsck" ] && e2fsck=$dir/e2fsck
	[ -z "$resize2fs" ] && [ -x "$dir/resize2fs" ] && resize2fs=$dir/resize2fs
done

if [ -z "$e2fsck" ] || [ -z "$resize2fs" ]; then
	echo "resize-system: no e2fsck or resize2fs in this recovery, skipped"
	exit 0
fi

"$e2fsck" -fy "$dev"
"$resize2fs" "$dev" && "$e2fsck" -fy "$dev"
exit 0
