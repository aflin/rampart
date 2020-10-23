# Rampart (rp) #

Cmake version 3.13 required.

## Compiling on Macos ##

You will need to install the following dependencies:

```
brew install libidn2 gawk python3
```

Then, after cloning, you can do the following to build:

```
mkdir rampart/build
cd rampart/build
cmake ../
make
make install
```

## Compiling on Linux (including raspberry pi) ##
```
apt install flex bison libidn2-dev libldap2-dev python3
```
or
```
yum install libidn2-devel openldap-devel flex bison python3
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
