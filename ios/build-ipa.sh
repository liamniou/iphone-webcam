#!/bin/bash
# Builds an unsigned IPhoneWebcam.ipa for sideloading (Sideloadly / AltStore sign it with your Apple ID).
# Usage: ./build-ipa.sh [version] [build-number]      (requires Xcode; installs XcodeGen via Homebrew if needed)
set -euo pipefail
cd "$(dirname "$0")"

VERSION="${1:-1.0.0}"
BUILD_NUMBER="${2:-1}"

command -v xcodegen >/dev/null || brew install xcodegen
xcodegen generate

rm -rf build
xcodebuild \
  -project IPhoneWebcam.xcodeproj \
  -target IPhoneWebcam \
  -configuration Release \
  -sdk iphoneos \
  SYMROOT="$PWD/build" \
  MARKETING_VERSION="$VERSION" \
  CURRENT_PROJECT_VERSION="$BUILD_NUMBER" \
  CODE_SIGNING_ALLOWED=NO \
  CODE_SIGNING_REQUIRED=NO \
  CODE_SIGN_IDENTITY="" \
  build

mkdir -p build/ipa/Payload
cp -R build/Release-iphoneos/IPhoneWebcam.app build/ipa/Payload/
(cd build/ipa && zip -qry "../IPhoneWebcam-$VERSION-unsigned.ipa" Payload)
echo "Built ios/build/IPhoneWebcam-$VERSION-unsigned.ipa"
