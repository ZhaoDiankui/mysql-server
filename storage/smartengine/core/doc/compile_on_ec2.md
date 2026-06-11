<h2>Install dependencies</h2>

```
sudo yum-config-manager --add-repo http://mirror.centos.org/centos/7/sclo/x86_64/rh/
sudo yum install -y wget
wget http://mirror.centos.org/centos/7/os/x86_64/Packages/libgfortran5-8.3.1-2.1.1.el7.x86_64.rpm
sudo yum install -y libgfortran5-8.3.1-2.1.1.el7.x86_64.rpm
sudo yum install -y devtoolset-11 --nogpgcheck
sudo yum install -y dnf
sudo dnf install -y ninja-build
scl enable devtoolset-11 bash
```
<h2>Compile</h2>

```
mkdir build
cd build
cmake3 ../ -B ./ \
  -DCMAKE_INSTALL_PREFIX="/u01/mysql" \
  -DWITH_SSL="system" \
  -DCMAKE_BUILD_TYPE="Debug" \
  -DWITH_WESQL_TEST=1   \
  -DDOWNLOAD_BOOST=1 \
  -DWITH_BOOST="../extra/"
make -j16
```

