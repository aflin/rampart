# Rampart (rp) #

Cmake version 3.13 required.

## Compiled Versions ##

Compiled versions for several platforms can be found [here](https://rampart.dev/downloads/)

## Compiling on Macos ##

You will need to install the following dependencies:

```
brew install libidn2 gawk python3
```

For a more complete build of the rampart-python module:

```
brew install tcl-tk gdbm xz readline sqlite
```

Then, after cloning, you can do the following to build:

```
mkdir rampart/build
cd rampart/build
cmake ../
#or cmake -DMACOS_UNIVERSAL=ON ../ # for universal binary
make
make install
```

## Compiling on Linux (including raspberry pi) ##
```
apt install flex bison libidn2-dev libldap2-dev python3 zlib1g-dev
```
or
```
yum install libidn2-devel openldap-devel flex bison python3 zlib-devel
```

For a more complete build of the rampart-python module:

```
apt install libsqlite3-dev uuid-dev tcl-dev tk-dev libgdbm-dev libbz2-dev liblzma-dev libffi-dev libgdbm-compat-dev libncurses-dev libreadline-dev
```
or
```
yum install sqlite-devel tcl-devel tk-devel libuuid-devel readline-dev ncurses-devel bzip2-devel gdbm-devel xz-devel
```

Then, after cloning, you can do the following to build:

```
mkdir rampart/build
cd rampart/build
cmake ../
make
make install
```

Most of the relevant files will be in `/usr/local/rampart` with links in /usr/local/bin. You can run scripts using the following:
```
rampart <js-file-path>
```
