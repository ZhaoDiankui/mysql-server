#!/bin/sh

shell_quote_string() {
  echo "$1" | sed -e 's,\([^a-zA-Z0-9/_.=-]\),\\\1,g'
}

usage () {
    cat <<EOF
Usage: $0 [OPTIONS]
    The following options may be given :
        --builddir=DIR              Absolute path to the dir where all actions will be performed
        --source_code=DIR           Source code dir in builddir
        --build_rpm                 If it is 1 rpm will be built
        --install_deps              Install build dependencies(root previlages are required)
        --branch                    Branch for build
        --prerelease                RPM prerelease version(default = 1)
        --snapshot                  If it is 1 RPM version with snapshot
        --date                      If it is 1 RPM version with date
        --mini_debug                If it is 1 build to use mini debug
        --without_debug             If it is 1 do not build debug version
        --without_debuginfo         If it is 1 disable debuginfo package and do not build debug package
        --without_devel             If it is 1 do not build devel package
        --without_test              If it is 1 do not build test package
        --parallel_build_jobs       Number of jobs to compile using make. By Default, it depends on the number of cpu cores
        --help) usage ;;
Example $0 --builddir=/home/mysql/code/ --source_code=mysql-server --build_rpm=1
EOF
        exit 1
}

append_arg_to_args () {
  args="$args "$(shell_quote_string "$1")
}

parse_arguments() {
    pick_args=
    if test "$1" = PICK-ARGS-FROM-ARGV
    then
        pick_args=1
        shift
    fi

    for arg do
        val=$(echo "$arg" | sed -e 's;^--[^=]*=;;')
        case "$arg" in
            # these get passed explicitly to mysqld
            --builddir=*) WORKDIR="$val" ;;
            --source_code=*) SRCDIR="$val" ;;
            --build_rpm=*) RPM="$val" ;;
            --branch=*) BRANCH="$val" ;;
            --repo=*) REPO="$val" ;;
            --install_deps=*) INSTALL="$val" ;;
            --prerelease=*) PRERELEASE="$val" ;;
            --snapshot=*) SNAPSHOT="$val" ;;
            --date=*) DATE="$val" ;;
            --mini_debug=*) MINIDEBUG="$val" ;;
            --without_debug=*) NODEBUG="$val" ;;
            --without_debuginfo=*) NODEBUGINFO="$val" ;;
            --without_devel=*) NODEVEL="$val" ;;
            --without_test=*) NOTEST="$val" ;;
            --parallel_build_jobs=*) PARALLELJOBS="$val" ;;
            --help) usage ;;
            *)
              if test -n "$pick_args"
              then
                  append_arg_to_args "$arg"
              fi
              ;;
        esac
    done
}

get_sources(){
    cd ${WORKDIR}/${SRCDIR} 
    if [ ! -z "$BRANCH" ]
    then
#        git reset --hard
#        git clean -xdf
    git checkout "$BRANCH"
    else
        BRANCH=$(git rev-parse --abbrev-ref HEAD)
    fi

    REVISION=$(git rev-parse --short=7 HEAD)
    #
    echo >> ../wesql-server-8.0.properties
    echo "REVISION=${REVISION}" >> ../wesql-server-8.0.properties
    BRANCH_NAME="${BRANCH}"
    echo "BRANCH_NAME=${BRANCH_NAME}" >> ../wesql-server-8.0.properties
    echo "BOOST_PACKAGE_NAME=${BOOST_PACKAGE_NAME}" >> ../wesql-server-8.0.properties

    if [ -z "${DESTINATION:-}" ]; then
        export DESTINATION=experimental
    fi
    echo "DESTINATION=${DESTINATION}" >> ../wesql-server-8.0.properties

    return
}

get_system(){
    if [ -f /etc/redhat-release ]; then
	GLIBC_VER_TMP="$(rpm glibc -qa --qf %{VERSION})"
        RHEL=$(rpm --eval %rhel)
        ARCH=$(echo $(uname -m) | sed -e 's:i686:i386:g')
        OS_NAME="el$RHEL"
        OS="rpm"
    else
	GLIBC_VER_TMP="$(dpkg-query -W -f='${Version}' libc6 | awk -F'-' '{print $1}')"
        ARCH=$(uname -m)
        OS_NAME="$(lsb_release -sc)"
        OS="deb"
    fi
    export GLIBC_VER=".glibc${GLIBC_VER_TMP}"
    return
}

install_deps() {
    if [ $INSTALL = 0 ]
    then
        echo "Dependencies will not be installed"
        return;
    fi
    if [ ! $( id -u ) -eq 0 ]
    then
        echo "It is not possible to instal dependencies. Please run as root"
        exit 1
    fi

    if [ "x$OS" = "xrpm" ]; then
        RHEL=$(rpm --eval %rhel)
        ARCH=$(echo $(uname -m) | sed -e 's:i686:i386:g')
        if [ "${RHEL}" -lt 8 ]; then
            yum -y install yum-utils oracle-epel-release-el7 oracle-softwarecollection-release-el7
            yum-config-manager --enable ol7_optional_latest
        fi
        yum -y update
        yum -y install git tar make cmake3 rpm-build libtool
        yum -y install time gcc gcc-c++ perl sshpass
        yum -y install bison pam-devel ncurses-devel readline-devel openssl-devel zlib-devel libaio-devel numactl-devel  libcurl-devel openldap-devel libcurl-devel
        yum -y install perl-Time-HiRes perl-Env perl-Data-Dumper perl-JSON perl-Digest perl-Digest-MD5 perl-Digest-Perl-MD5 || true
        yum -y install libicu-devel lz4-devel libevent-devel libzstd-devel
        yum -y install zstd libzstd
        if [ "${RHEL}" -lt 8 ]; then
	        if [ "x$ARCH" = "xaarch64" ]; then
                yum -y install devtoolset-10-gcc-c++ devtoolset-10-binutils devtoolset-10-gcc
                source /opt/rh/devtoolset-10/enable
	        else
                yum -y install devtoolset-11-gcc-c++ devtoolset-11-binutils devtoolset-11-gcc
                source /opt/rh/devtoolset-11/enable
            fi
        else
            yum -y install gcc-toolset-12-gcc gcc-toolset-12-gcc-c++ gcc-toolset-12-binutils gcc-toolset-12-annobin-annocheck gcc-toolset-12-annobin-plugin-gcc
            source /opt/rh/gcc-toolset-12/enable
            yum -y --enablerepo=ol"${RHEL}"_codeready_builder install rpcgen
        fi
    else
        echo "It is not possible to build rpm here"
        exit 1
    fi
    return;
}

build_rpm(){
    if [ $RPM = 0 ]
    then
        echo "RPM will not be created"
        return;
    fi
    if [ "x$OS" = "xdeb" ]
    then
        echo "It is not possible to build rpm here"
        exit 1
    fi

    if [ $PARALLELJOBS = 0 ]
    then
      PARALLELJOBS=$(grep -c "processor" /proc/cpuinfo)
    fi

    cd ${WORKDIR}
    mkdir -vp rpmbuild/{SOURCES,SPECS,BUILD,SRPMS,RPMS}

    RHEL=$(rpm --eval %rhel)
    ARCH=$(echo $(uname -m) | sed -e 's:i686:i386:g')
    #
    echo "RHEL=${RHEL}" >> ../wesql-server-8.0.properties
    echo "ARCH=${ARCH}" >> ../wesql-server-8.0.properties


    RPM_RELEASE="${PRERELEASE}"
    if [ $DATE = 1 ]; then
        RPM_RELEASE+=".$(date "+%Y%m%d")"
    fi
    if [ $SNAPSHOT = 1 ]; then
        RPM_RELEASE+=".g${REVISION}"
    fi

    mkdir -p ${WORKDIR}/generate_spec
    cd ${WORKDIR}/generate_spec
    cmake3 ../${SRCDIR} -B ./ -DWITH_SSL=system -DDOWNLOAD_BOOST=1 -DWITH_BOOST=${WORKDIR} \
      -DWITH_ZLIB=bundled -DWITHOUT_GROUP_REPLICATION=1 -DWITHOUT_NDBCLUSTER=1
    rpmbuild --undefine=_disable_source_fetch --define "revision ${REVISION}" --define "rpm_release ${RPM_RELEASE}" \
      --define "with_mini_debug ${MINIDEBUG}" --define "without_debug ${NODEBUG}" --define "without_debuginfo ${NODEBUGINFO}" \
      --define "without_devel ${NODEVEL}" --define "without_test ${NOTEST}" --define "_smp_mflags -j${PARALLELJOBS}" \
      --define "_topdir ${WORKDIR}/rpmbuild" --define "_builddir ${WORKDIR}/" --define "_sourcedir ${WORKDIR}" --define "src_base ${SRCDIR}" \
      -bb packaging/build-wesql/wesql.spec
    cd ${WORKDIR}

    return_code=$?
    if [ $return_code != 0 ]; then
        exit $return_code
    fi
    mkdir -p ${WORKDIR}/rpm
    mv rpmbuild/RPMS/*/*.rpm ${WORKDIR}/rpm/
}

#main

CURDIR=$(pwd)
VERSION_FILE=$CURDIR/wesql-server-8.0.properties
args=
WORKDIR=/home/mysql/code
SRCDIR=mysql-server
RPM=0
OS_NAME=
ARCH=
OS=
PRERELEASE=1
SNAPSHOT=0
INSTALL=1
REVISION=0
BRANCH=
DATE=0
MINIDEBUG=0
NODEBUG=0
NODEBUGINFO=0
NODEVEL=0
NOTEST=0
PARALLELJOBS=0
MECAB_INSTALL_DIR="${WORKDIR}/mecab-install"
BOOST_PACKAGE_NAME=boost_1_77_0
parse_arguments PICK-ARGS-FROM-ARGV "$@"

get_system
install_deps
get_sources
build_rpm
