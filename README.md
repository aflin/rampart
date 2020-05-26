# Rampart (rp) #

## Compiling on Macos##

You will need to install the following dependencies:

```
brew install oniguruma
brew install libevhtp
brew install curl
brew install gawk
```

Then, after cloning, you can do the following to build:

```
cd rampart/build
cmake ../
make
```

Most of the relevant files will be in `build/rp`. You can run them using the following: 

```
./rp <js-file-path>
```
It may be necessary to up the open files limits when running the webserver:
```
ulimit -n 16384
```
