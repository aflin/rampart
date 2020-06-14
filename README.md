# Rampart (rp) #

## Compiling on Macos ##

You will need to install the following dependencies:

```
brew install libidn2
```

Then, after cloning, you can do the following to build:

```
mkdir rampart/build
cd rampart/build
cmake ../
make
```

Most of the relevant files will be in `build/src`. You can run them using the following: 

```
./rp <js-file-path>
```
You may also need to do this before running the webserver:
```
ulimit -n 16384
```
