# 使用的开源软件说明

## libboundscheck

### 代码来源说明

该仓被标准库及周边组件源码依赖，仓库源码地址为 [third_party_bounds_checking_function](https://gitcode.com/openharmony/third_party_bounds_checking_function)，版本为 [OpenHarmony-v6.0-Release](https://gitcode.com/openharmony/third_party_bounds_checking_function/tags/OpenHarmony-v6.0-Release)。

该开源软件被标准库及周边组件以包含头文件的方式使用，并通过链接库（动态库或静态库）的方式依赖。

### 构建说明

本仓库通过 CMake 作为子目标项目引入，编译过程中会自动完成构建并建立依赖关系，具体构建参数请参见 [CMakeLists.txt](../CMakeLists.txt) 文件。

## PCRE2

### 代码来源说明

PCRE2 是一个 Perl 兼容的正则表达式库，仓颉语言使用 PCRE2 库完成正则表达式相关功能。

该仓标准库源码依赖，其基于开源代码 [PCRE2 OpenHarmony-v6.0-Release](https://gitcode.com/openharmony/third_party_pcre2/tags/OpenHarmony-v6.0-Release) 进行编译。

该开源软件被标准库以包含头文件的方式使用，并通过链接库（动态库或静态库）的方式依赖。

### 构建说明

本仓库通过 CMake 作为外部项目引入，具体构建参数请参见 [PCRE2.cmake](./cmake/Pcre2.cmake) 文件。

## flatbuffers

### 代码来源说明

FlatBuffers 是一个高效的跨平台、跨语言序列化库，仓颉语言使用 FlatBuffers 库完成编译器数据到指定格式的序列化反序列化操作。

该仓被标准库源码依赖，其基于开源代码 [flatbuffers OpenHarmony-v6.0-Release](https://gitcode.com/openharmony/third_party_flatbuffers/tags/OpenHarmony-v6.0-Release) 进行定制化修改。

该开源软件被标准库及周边组件以包含头文件的方式使用，并通过链接库（动态库或静态库）的方式依赖。

### 三方库 patch 说明

为了提供 C++ 到仓颉语言的序列化反序列化能力，本项目对 FlatBuffers 库进行定制化修改，导出为 [flatbufferPatch.diff](./flatbufferPatch.diff) 文件。

### 构建说明

本仓库通过 CMake 作为子目标项目引入，编译过程中会自动完成构建并建立依赖关系，具体构建参数请参见 [Flatbuffer.cmake](./cmake/Flatbuffer.cmake) 文件。

开发者也可以手动下载 [flatbuffers](https://gitcode.com/openharmony/third_party_flatbuffers.git) 源码，并应用 patch 文件，命令如下：

```shell
mkdir -p third_party/flatbuffers
cd third_party/flatbuffers
git clone https://gitcode.com/openharmony/third_party_flatbuffers.git -b OpenHarmony-v6.0-Release ./
git apply ../flatbufferPatch.diff
```

构建项目时，则直接使用 third_party/flatbuffers 目录源码进行构建。