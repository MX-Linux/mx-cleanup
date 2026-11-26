#!/bin/bash
set -e
cd -- "$(dirname -- "$0")"

# Keep filenames in step with the paths rewritten below. Debian native
# source archives may omit Arch packaging files.
for path in mx-cleanup.desktop images/mx-cleanup.png mx-cleanup.install \
    arch/mx-cleanup.install debian/mx-cleanup.manpages help/mx-cleanup.1 \
    help/mx-cleanup.html scripts/org.mxlinux.pkexec.mx-cleanup-helper.policy.in; do
    if [[ -f "$path" ]]; then
        mv -- "$path" "${path/mx-cleanup/cleanup}"
    fi
done

sed -i '/<file>images\/logo\.svg<\/file>/d' images.qrc
sed -i '/<property name="pixmap">/{N;N;/images\/logo\.svg/d;}' mainwindow.ui
rm -f -- images/logo.svg translations/mx-*

# Rewrite source text only. Keep compiled files, release archives, and
# separate checkouts out of the substitution.
find . \( -name .git -o -name aur -o -name build -o -name debs -o -name 'obj-*' \) -prune -o \
    -type f ! -name debrand.sh -exec grep -IlZ -e 'MX Cleanup' -e mx-cleanup {} + |
    xargs -0 -r sed -i -e 's/MX Cleanup/Cleanup/g' -e 's/mx-cleanup/cleanup/g'
