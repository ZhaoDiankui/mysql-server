#!/bin/bash
cd extra/aws-sdk-cpp
#git clone --branch <aws-sdk-cpp-tag> https://github.com/aws/aws-sdk-cpp
git clone --branch 1.11.283 https://github.com/aws/aws-sdk-cpp
./prefetch_crt_dependency.sh



