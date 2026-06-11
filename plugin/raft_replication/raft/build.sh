#!/bin/bash
#
# Script for Dev's daily work.  It is a good idea to use the exact same
# build options as the released version.

get_key_value()
{
  echo "$1" | sed 's/^--[a-zA-Z_-]*=//'
}

usage()
{
cat <<EOF
Usage: $0 [-t debug|release]
       Or
       $0 [-h | --help]
  -t                      Select the build type [debug|release].
  -o                      install directory
  -h, --help              Show this help message.

Note: this script is intended for internal use by X-Paxos developers.
EOF
}

parse_options()
{
  while test $# -gt 0
  do
    case "$1" in
    -t=*)
      build_type=`get_key_value "$1"`;;
    -t)
      shift
      build_type=`get_key_value "$1"`;;
    -o=*)
      install_dir=`get_key_value "$1"`;;
    -o)
      shift
      install_dir=`get_key_value "$1"`;;
    -h | --help)
      usage
      exit 0;;
    *)
      echo "Unknown option '$1'"
      exit 1;;
    esac
    shift
  done
}

dump_options()
{
  echo "Dumping the options used by $0 ..."
  echo "build_type=$build_type"
  echo "install_dir=$install_dir"
}

build_type="release"
install_dir="output"
build_dir="bu"

parse_options "$@"
dump_options

if [ x"$build_type" = x"debug" ]; then
  debug="ON"
elif [ x"$build_type" = x"release" ]; then
  debug="OFF"
else
  echo "Invalid build type, it must be \"debug\" or \"release\"."
  exit 1
fi

CWD="`pwd`"
if [[ "${install_dir:0:1}" == "/" ]]; then
dest_dir=$install_dir
else
dest_dir=$CWD"/"$install_dir
fi

if [ ! -d $build_dir ];then
  mkdir $build_dir
fi

cd $build_dir

# modify this cmake script for you own needs
cmake3 -D CMAKE_INSTALL_PREFIX=$dest_dir -D WITH_DEBUG=$debug -D BUILD_WITH_PROTOBUF="bundled" -D WITH_TEST=ON -B ./ $CWD
