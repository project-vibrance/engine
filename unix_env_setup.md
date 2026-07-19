### Building dependencies
- Ensure you have installed [Homebrew](brew.sh)
- Then do the following commands in terminal:
```sh
brew install mingw-w64 make
brew install cmake
```

### Building FreeType
- Install [freetype-2-14-3.tar.xz](https://sourceforge.net/projects/freetype/files/freetype2/2.14.3/freetype-2.14.3.tar.xz/download) first
- Extract the tar file and build the source
```sh
tar -xf freetype-2.14.3.tar.xz
cd freetype-2.14.3
cmake -DCMAKE_BUILD_TYPE=Release ..
make
make install
```
- Move `freetype.2.14.3` along its contents to another directory like `~/Development` for example
- To use in `.env.cmake`, call:
```cmake
set(FREETYPE_PATH "/Users/{you}/Development/freetype-2.14.3")
```