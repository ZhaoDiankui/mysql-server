#!/bin/bash

# create build_version.cc file. Only create one if it doesn't exists or if it is different
# this is so that we don't rebuild mysqld every time
bv=storage/smartengine/core/util/build_version.cc
date=$(date +%F)
git_sha=$(pushd ./storage/smartengine/core >/dev/null && git rev-parse  HEAD 2>/dev/null && popd >/dev/null)
if [ ! -f $bv ] || [ -z $git_sha ] || [ ! `grep $git_sha $bv` ]
then
echo "#include \"build_version.h\"
const char* smartengine_build_git_sha =
\"smartengine_build_git_sha:$git_sha\";
const char* smartengine_build_git_date =
\"smartengine_build_git_date:$date\";
const char* smartengine_build_compile_date = __DATE__;" > $bv
fi