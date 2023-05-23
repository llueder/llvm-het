# MELF LLVM lld

This is a LLVM 13.0.0 fork of the repository available at https://github.com/llvm/llvm-project.

## Installation

The following example shows how to build the lld with **ninja**.

### Steps

**Important:** You may want to ensure building the release config to reduce memory pressure during building.

```bash
$ mkdir build && cd build
$ cmake -G Ninja -DLLVM_ENABLE_PROJECTS=lld -DCMAKE_BUILD_TYPE=Release ../src/llvm
$ ninja # actually starts the build process. Might take a while.
```

After that `lld` can be found inside the build folder.
Create a symlink to reference to it with the name `ld.lld`.

```bash
$ ln -s /path/to/repository/build/bin/lld ld.lld
```

or add it to the PATH environment variable

```bash
$ export PATH=/path/to/repository/build/bin:$PATH
```

Now you can link multivariant ELFs.