#!/bin/bash
# Builds the LV2 download: a tarball holding the .lv2 bundle, to extract into ~/.lv2 or /usr/lib/lv2.
#
# Usage, from the plug-in's folder, after building it in Release in build/, with lv2-dev installed:
#   make-lv2-tarball.sh <version> <output folder>
set -euo pipefail

version=$1
output=$2

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cp -r build/LV2/RabidPhatt.lv2 "$work/"

# The licences have to accompany the binary. LV2's headers put a little of their code into it.
licences=$work/RabidPhatt.lv2/Licenses
mkdir -p "$licences" "$output"
cp LICENSE "$licences/RabidPhatt-LICENSE.txt"
cp external/vst3sdk/LICENSE.txt "$licences/VST3-SDK-LICENSE.txt"
cp external/vst3sdk/vstgui4/LICENSE "$licences/VSTGUI-LICENSE.txt"
cp external/hadronychecore/LICENSE "$licences/HadronycheCore-LICENSE.txt"
cp /usr/share/doc/lv2-dev/copyright "$licences/LV2-LICENSE.txt"
cp installer/NOTICE.txt "$licences/NOTICE.txt"

file=RabidPhatt-$version-Linux-x86_64-LV2.tar.gz
tar -czf "$output/$file" --owner=0 --group=0 -C "$work" RabidPhatt.lv2

echo "Built $output/$file"
