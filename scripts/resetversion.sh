#!/bin/sh

gitrev=`git rev-parse --short HEAD`
version="1.8.${gitrev}"
echo "Updating to version=$version"
tf=`mktemp`
jq --arg VERSION "$version" '.version=$VERSION' plugin.json > $tf
mv $tf plugin.json

