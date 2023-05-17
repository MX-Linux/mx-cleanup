#!/bin/bash

# rename files
mv mx-cleanup.desktop cleanup.desktop
mv mx-cleanup.png cleanup.png

# remove logo
rm images/logo.svg

# remove mx-* translation files
rm translations/mx-*

# remove "MX " and "mx-"
find . -type f \( -not -wholename "$0" -not -path "./.git/*" \) -exec sed -i "s/MX Cleanup/Cleanup/g" {} +
find . -type f \( -not -wholename "$0" -not -path "./.git/*" \) -exec sed -i "s/mx-cleanup/cleanup/g" {} +
