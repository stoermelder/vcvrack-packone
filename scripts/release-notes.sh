#!/bin/bash

cat <<- EOH
## Automatically generated PackOne build

This build is automatically generated every time I push my changes. As such it is the latest version of the code and may be unstable, unusable, unsuitable for human consumption, and so on.

The latest stable build can be found at the [VCV Library](https://vcvrack.com/plugins.html#packone).

These assets were built against
https://vcvrack.com/downloads/Rack-SDK-2.0.0-win.zip
https://vcvrack.com/downloads/Rack-SDK-2.0.0-mac.zip
https://vcvrack.com/downloads/Rack-SDK-2.0.0-lin.zip

The build date and most recent commits are:
EOH
date
echo ""
echo "Most recent commits:"
echo ""
git log --pretty=oneline | head -5