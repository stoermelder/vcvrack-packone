#!/bin/bash

set -e
set -o xtrace
RACK_FROM_SOURCE=1

help_message()
{
    cat <<EOF
What, this isn't build-osx.sh! Come on.

But I read the directory RACK_INSTALL_DIR and use a default if that's not set

    --get-rack      Downloads rack stuff to a well named spot
    --build         Builds
    --run           Builds and runs
    --debug         Builds and debugs

If RACK_INSTALL_DIR isn't set, I default to ~/dev/VCVRack/V1.0

A typical session could be

  ./scripts/buildutil.sh --get-rack
  ./scripts/buildutil.sh --run

EOF
}

un=`uname`
sdkversion=1.0.0
sdk="https://vcvrack.com/downloads/Rack-SDK-${sdkversion}.zip"
slug=`jq -r '{slug} | .[]' plugin.json `

if [ $un = "Darwin" ]; then
    runtime="https://vcvrack.com/downloads/Rack-${sdkversion}-mac.zip"
    plugin=plugin.dylib
    mac=true
fi
if [ $un = "Linux" ]; then
    runtime="https://vcvrack.com/downloads/Rack-${sdkversion}-lin.zip"
    plugin=plugin.so
fi
if [ -z "$runtime" ]; then
    runtime="https://vcvrack.com/downloads/Rack-${sdkversion}-win.zip"
    plugin=plugin.dll
fi

if [ -z "$RACK_INSTALL_DIR" ]; then
    RACK_INSTALL_DIR="${HOME}/dev/VCVRack/V1.0/"
    mkdir -p $RACK_INSTALL_DIR
fi    

if [ -z "$RACK_FROM_SOURCE" ]; then
    rd=$RACK_INSTALL_DIR/SDK/Rack-SDK
    ed=$RACK_INSTALL_DIR/SDK/
    ud=User-SDK
else
    rd=$RACK_INSTALL_DIR/Source/Rack
    ed=$RACK_INSTALL_DIR/Source/
    ud=User-SRC
fi

get_rack_sdk()
{
    echo "Getting rack into ${RACK_INSTALL_DIR}/SDK"
    mkdir -p ${RACK_INSTALL_DIR}/SDK
    cd $RACK_INSTALL_DIR/SDK
    curl -o Rack_Runtime.zip $runtime
    curl -o Rack_SDK.zip $sdk

    unzip Rack_Runtime.zip
    unzip Rack_SDK.zip

    rm Rack_Runtime.zip
    rm Rack_SDK.zip
}

build_rack()
{
    if [ -z "$RACK_FROM_SOURCE" ]; then
        echo "Please set RACK_FROM_SOURCE"
        exit 1
    fi
    mkdir -p ${RACK_INSTALL_DIR}/Source
    cd ${RACK_INSTALL_DIR}/Source
    rm -rf Rack
    git clone https://github.com/vcvrack/Rack.git
    cd Rack
    git checkout v1
    git submodule update --init --recursive
    make -j 4 dep
    make -j 4
}

run_rack()
{
    if [ -z "$mac" ]; then
        cd ${ed}/Rack
        ./Rack -u "$RACK_INSTALL_DIR/${ud}" 
    else
        if [ -z "$RACK_FROM_SOURCE" ]; then
            cd $RACK_INSTALL_DIR
            "${ed}/Rack.app/Contents/MacOS/Rack" -u "$RACK_INSTALL_DIR/${ud}"
        else
            cd ${ed}/Rack
            ./Rack -u "$RACK_INSTALL_DIR/${ud}" 
        fi
    fi
}

clean()
{
    RACK_DIR=$rd make clean
}

make_module()
{
    echo RACK_DIR=$rd make -k -j 4 $1
    RACK_DIR=$rd make -k -j 4 $1
}

install_module()
{
    make_module "dist"
    mkdir -p $RACK_INSTALL_DIR/${ud}/plugins
    mv dist/*zip $RACK_INSTALL_DIR/${ud}/plugins
    cd $RACK_INSTALL_DIR/${ud}/plugins
    unzip -o *zip
}

install_module_light()
{
    slug=`jq -r '{slug} | .[]' plugin.json `
    make_module "all"
    mkdir -p $RACK_INSTALL_DIR/${ud}/plugins/${slug}
    cp $plugin $RACK_INSTALL_DIR/${ud}/plugins/${slug}
}

make_images()
{
        pushd $RACK_INSTALL_DIR
        "$RACK_INSTALL_DIR/Rack.app/Contents/MacOS/Rack" -u "$RACK_INSTALL_DIR/${ud}" -p 2
        popd
        slug=`jq -r '{slug} | .[]' plugin.json `
        mkdir -p ./docs/screenshots
        mv ${RACK_INSTALL_DIR}/${ud}/screenshots/${slug}/*.png ./docs/screenshots
}


command="$1"

case $command in
    --help)
        help_message
        ;;
    --get-rack)
        get_rack_sdk
        ;;
    --build-rack)
        build_rack
        ;;
    --make)
        if [ -z "$2" ]; then
            cm="all"
        else
            cm=$2
        fi
        make_module "$cm"
        ;;
    --install)
        install_module
        ;;
    --run)
        run_rack
        ;;
    --br)
        make_module dist
        install_module_light
        run_rack
        ;;
    --bir)
        make_module "all"
        install_module
        run_rack
        ;;
    --images)
        make_images
        ;;
    --clean)
	    clean
	    ;;
    *)
        help_message
        ;;
esac
