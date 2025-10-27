// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#include "CjFileLoader.h"

#include "ExceptionManager.inline.h"
#include "ObjectManager.inline.h"
#include "TypeInfoManager.h"
namespace MapleRuntime {

void CJFileLoader::Fini()
{
    ClearLoadedFiles();
}

void CJFileLoader::RegisterLoadFile(Uptr fileMetaAddr)
{
    ScopedEntryTrace trace("CJRT_RegisterLoadFile");
    BaseFile* file = GetBaseFileByMetaAddr(fileMetaAddr);
    if (file == nullptr) {
        return;
    }
    file->RegisterFile();
#ifndef __arm__
    AddPackageInfos(file);
#endif
    RegisterTypeInfoCreatedByFE(file);
    RegisterOuterTypeExtensions(file);
}

BaseFile* CJFileLoader::GetBaseFileByMetaAddr(Uptr fileMetaAddr)
{
    BaseFile* file = nullptr;
    VisitBaseFile([&file, &fileMetaAddr](BaseFile* cJfile) {
        if (cJfile->GetFileMetaAddr() == fileMetaAddr) {
            file = cJfile;
            return true;
        } else {
            return false;
        }
    });
    return file;
}

void CJFileLoader::UnregisterLoadFile(Uptr fileMetaAddr)
{
    BaseFile* file = GetBaseFileByMetaAddr(fileMetaAddr);
    if (file != nullptr) {
        RemoveLoadedFiles(file);
    }
}
void CJFileLoader::AddLoadedFiles(BaseFile* baseFile) { loadedFiles.push_back(baseFile); }

BaseFile* CJFileLoader::CreateFileRefFromAddr(Uptr fileMetaAddr)
{
    auto getBinaryInfoFromAddressFunc = GetBinaryInfoFromAddressFunc();
    CHECK(getBinaryInfoFromAddressFunc != nullptr);
    Os::Loader::BinaryInfo binInfo;
    int isGetBinInfoSuccess = getBinaryInfoFromAddressFunc(reinterpret_cast<void*>(fileMetaAddr), &binInfo);
    if (isGetBinInfoSuccess == 0) {
        isGetBinInfoSuccess = Os::Loader::GetBinaryInfoFromAddress(reinterpret_cast<void*>(fileMetaAddr), &binInfo);
    }
    CHECK(isGetBinInfoSuccess != 0);
    BaseFile* file = BaseFile::CreateCJFile(FileType::C_FILE, CString(binInfo.filePathName), fileMetaAddr);
    if (file == nullptr) {
        return nullptr;
    }
    return file;
}

void CJFileLoader::AddPackageInfos(BaseFile* baseFile)
{
    Uptr packageInfoBase = baseFile->GetPackageInfoBase();
    U32 pkgTotalSize = baseFile->GetPackageInfoTotalSize();
    while (pkgTotalSize > 0) {
        PackageInfo* packageInfo = reinterpret_cast<PackageInfo*>(packageInfoBase);
        const char* pkgName = packageInfo->GetPackageName();
        auto pkgIt = packageInfos.find(pkgName);
        if (pkgIt == packageInfos.end()) {
            packageInfos.insert({ pkgName, packageInfo });
            // record the relation between file and the packageInfo,
            // identify whether multiple packages exist in a file.
            auto fileIt = filePackageMap.find(baseFile->GetBaseName().Str());
            if (fileIt == filePackageMap.end()) {
                std::vector<PackageInfo*> pkgs { packageInfo };
                filePackageMap.insert({ baseFile->GetBaseName().Str(), pkgs });
            } else {
                fileIt->second.push_back(packageInfo);
            }
        }

        size_t packageInfoSize = packageInfo->GetPackageSize();
        if (pkgTotalSize >= packageInfoSize) {
            pkgTotalSize -= packageInfoSize;
        } else {
            break;
        }
        packageInfoBase += packageInfoSize;
    }
}

bool CJFileLoader::FileHasLoaded(const char* path)
{
    CString baseName = Os::Path::GetBaseName(path);
    auto fileIt = filePackageMap.find(baseName.Str());
    if (fileIt != filePackageMap.end()) {
        return true;
    }
    return false;
}

bool CJFileLoader::FileHasMultiPackage(const char* path)
{
    CString baseName = Os::Path::GetBaseName(path);
    auto fileIt = filePackageMap.find(baseName.Str());
    if (fileIt != filePackageMap.end() && fileIt->second.size() > 1) {
        return true;
    }
    return false;
}

void CJFileLoader::GetSubPackages(PackageInfo* packageInfo, std::vector<PackageInfo*> &subPackages)
{
    CString prefix = CString(packageInfo->GetPackageName()) + ".";
    for (auto &pkgInfoPair : packageInfos) {
        PackageInfo* pkgInfo = pkgInfoPair.second;
        if (CString(pkgInfo->GetPackageName()).StartWith(prefix)) {
            subPackages.emplace_back(pkgInfo);
        }
    }
}

void CJFileLoader::VisitExtenionData(const std::function<bool(ExtensionData* ed)>& f, TypeTemplate* tt) const
{
    if (extensionDatas.find(tt) == extensionDatas.end()) {
        return;
    }
    auto range = extensionDatas.equal_range(tt);
    for (auto it = range.first; it != range.second; ++it) {
        if (f(it->second)) {
            return;
        }
    }
}

void CJFileLoader::ParseEnumCtor(TypeInfo* ti)
{
#ifdef __arm__
    return;
#endif
    if (ti->IsGenericTypeInfo()) {
        return TypeInfoManager::GetInstance()->ParseEnumInfo(
            ti->GetSourceGeneric(), ti->GetTypeArgNum(), ti->GetTypeArgs(), ti);
    }
    EnumInfo* ei = ti->GetEnumInfo();
    if (ei == nullptr || ei->GetNumOfEnumCtor() == 0 || ei->IsParsed()) {
        return;
    }
    U32 enumCtorNum = ei->GetNumOfEnumCtor();
    for (U32 idx = 0; idx < enumCtorNum; ++idx) {
        EnumCtorInfo* enumCtorInfo = ei->GetEnumCtor(idx);
        void* fn = reinterpret_cast<void*>(enumCtorInfo->GetCtorFn());
        if (fn == nullptr) {
            continue;
        }
        TypeInfo* enumTi = reinterpret_cast<TypeInfo*>(
            TypeTemplate::ExecuteGenericFunc(fn, ti->GetTypeArgNum(), ti->GetTypeArgs()));
        enumCtorInfo->SetTypeInfo(enumTi);
    }
    ei->SetParsed();
}

void CJFileLoader::RegisterTypeInfoCreatedByFE(BaseFile* baseFile)
{
    Uptr typeInfoBase = baseFile->GetTypeInfoBase();
    Uptr typeInfoEnd = typeInfoBase + baseFile->GetTypeInfoTotalSize();
    while (typeInfoBase < typeInfoEnd) {
        TypeInfo* ti = reinterpret_cast<TypeInfo*>(typeInfoBase);
        constexpr uint32_t typeInfoAlign = 16u;
        constexpr uint32_t sizeAlign = MRT_ALIGN(sizeof(TypeInfo), typeInfoAlign);
        typeInfoBase += sizeAlign;
        TypeInfoManager::GetInstance()->AddTypeInfo(ti);
        if (ti->IsEnum() || ti->IsTempEnum()) {
            ParseEnumCtor(ti);
        }
    }
    TypeInfoManager::GetInstance()->InitAnyAndObjectType();

    Uptr staticGIBase = baseFile->GetStaticGIBase();
    Uptr staticGIEnd = staticGIBase + baseFile->GetStaticGISize();
    staticGIs.clear();
    while (staticGIBase < staticGIEnd) {
        I32 offset = *reinterpret_cast<I32*>(staticGIBase);
#if defined(__APPLE__)
        TypeInfo* ti = reinterpret_cast<TypeInfo*>(staticGIBase - offset);
#else
        TypeInfo* ti = reinterpret_cast<TypeInfo*>(staticGIBase + offset);
#endif
        staticGIBase += sizeof(I32);
        staticGIs.push_back(ti);
        if (ti->IsEnum() || ti->IsTempEnum()) {
            continue;
        } else if (ti->IsGenericTypeInfo() && ti->ReflectInfoIsNull() && !ti->GetSourceGeneric()->ReflectInfoIsNull()) {
            TypeInfoManager::GetInstance()->FillReflectInfo(ti->GetSourceGeneric(), ti);
        }
    }
}

void CJFileLoader::RegisterOuterTypeExtensions(BaseFile* baseFile)
{
    Uptr extensionDataRefBase = baseFile->GetOuterTypeExtensionsBase();
    Uptr extensionDataRefEnd = extensionDataRefBase + baseFile->GetOuterTypeExtensionsSize();
    while (extensionDataRefBase < extensionDataRefEnd) {
        I32 offset = *reinterpret_cast<I32*>(extensionDataRefBase);
#ifdef __APPLE__
        ExtensionData* extensionData = reinterpret_cast<ExtensionData*>(extensionDataRefBase - offset);
#else
        ExtensionData* extensionData = reinterpret_cast<ExtensionData*>(extensionDataRefBase + offset);
#endif
        extensionDataRefBase += sizeof(I32);
        // for the extension of which target is a TypeInfo, since it cannot be used
        // in subsequent processes, we add MTable for it in advance so that it won't
        // need to be collected in `extensionDatas`.
        if (extensionData->TargetIsTypeInfo()) {
            TypeInfo* itf = extensionData->GetInterfaceTypeInfo();
            TypeInfoManager::GetInstance()->AddTypeInfo(itf);
            TypeInfo* ti = reinterpret_cast<TypeInfo*>(extensionData->GetTargetType());
            TypeInfoManager::GetInstance()->AddTypeInfo(ti);
            ti->AddMTable(itf, extensionData->GetFuncTable());
            continue;
        }
        TypeTemplate* tt = reinterpret_cast<TypeTemplate*>(extensionData->GetTargetType());
        extensionDatas.insert({ tt, extensionData });
    }
}

void CJFileLoader::GenerateMTableForStaticGI()
{
    ScopedEntryTrace trace("CJRT_GenerateMTableForStaticGI");
    for (auto ti : staticGIs) {
        U32 tiUUID = ti->GetUUID();
        TypeTemplate* tt = ti->GetSourceGeneric();
        if (tt == nullptr) {
            continue;
        }
        lazyInitStaticGIs.insert(tiUUID);
        ti->SetvExtensionDataStart(tt->GetvExtensionDataStart());
    }
}

PackageInfo* CJFileLoader::GetPackageInfoByPath(const char* path)
{
    CString baseName = Os::Path::GetBaseName(path);
    auto fileIt = filePackageMap.find(baseName.Str());
    if (fileIt == filePackageMap.end()) {
        return nullptr;
    }
    return fileIt->second[0];
}

void CJFileLoader::RemovePackageInfo(const char* path)
{
    CString baseName = Os::Path::GetBaseName(path);
    auto fileIt = filePackageMap.find(baseName.Str());
    if (fileIt != filePackageMap.end()) {
        for (auto pkgInfo : fileIt->second) {
            packageInfos.erase(pkgInfo->GetPackageName());
        }
        filePackageMap.erase(baseName.Str());
    }
}

PackageInfo* CJFileLoader::GetPackageInfo(const char* pkgName) const
{
    PackageInfo* pkgInfo = nullptr;
    auto it = packageInfos.find(pkgName);
    if (it != packageInfos.end()) {
        pkgInfo = it->second;
        if (!pkgInfo->IsVaild()) {
            return nullptr;
        }
        return pkgInfo;
    }
    return nullptr;
}

void CJFileLoader::RemoveLoadedFiles(BaseFile* baseFile)
{
    loadedFiles.remove(baseFile);
    baseFile->UnregisterFile();
    delete baseFile;
}

void CJFileLoader::VisitBaseFile(const std::function<bool(BaseFile*)>& f) const
{
    for (auto file : loadedFiles) {
        if (f(file)) {
            return;
        }
    }
}

TypeInfo* CJFileLoader::FindTypeInfoFromLoadedFiles(const char* typeInfoName)
{
    auto it = typeInfoCache.find(typeInfoName);
    if (it != typeInfoCache.end()) {
        return it->second;
    }
    CString pkgName;
    CString typeInfoNameStr = CString(typeInfoName);
    int idx = typeInfoNameStr.Find(':');
    if (idx < 0) {
        pkgName = "std.core";
    } else {
        pkgName = typeInfoNameStr.SubStr(0, idx);
    }
    auto pkgIt = packageInfos.find(pkgName.Str());
    if (pkgIt != packageInfos.end()) {
        PackageInfo* pkgInfo = pkgIt->second;
        TypeInfo* ti = pkgInfo->GetTypeInfo(typeInfoName);
        if (ti == nullptr) {
            return nullptr;
        }
        typeInfoCache.insert({ typeInfoName, ti });
        return ti;
    }
    return nullptr;
}

TypeTemplate* CJFileLoader::FindTypeTemplateFromLoadedFiles(const char* typeTemplateName)
{
    auto it = typeTemplateCache.find(typeTemplateName);
    if (it != typeTemplateCache.end()) {
        return it->second;
    }
    CString pkgName;
    CString typeTemplateNameStr = CString(typeTemplateName);
    int idx = typeTemplateNameStr.Find(':');
    if (idx < 0) {
        pkgName = "std.core";
    } else {
        pkgName = typeTemplateNameStr.SubStr(0, idx);
    }
    auto pkgIt = packageInfos.find(pkgName.Str());
    if (pkgIt != packageInfos.end()) {
        PackageInfo* pkgInfo = pkgIt->second;
        TypeTemplate* tt = pkgInfo->GetTypeTemplate(typeTemplateName);
        if (tt == nullptr) {
            return nullptr;
        }
        typeTemplateCache.insert({ typeTemplateName, tt });
        return tt;
    }
    return nullptr;
}

void CJFileLoader::RecordTypeInfo(TypeInfo* ti)
{
    typeInfoCache.insert({ ti->GetName(), ti });
}

void CJFileLoader::ClearLoadedFiles()
{
    VisitBaseFile([](BaseFile* baseFile) {
        baseFile->UnregisterFile();
        delete baseFile;
        return false;
    });
    loadedFiles.clear();
}

bool CJFileLoader::LibInit(const char* libName)
{
    BaseFile* baseFile = GetBaseFile(libName);
    if (baseFile == nullptr) {
        return false;
    }
    return DoInitImage(baseFile);
}

#ifdef __OHOS__
void CJFileLoader::RegisterLoadFunc(void* loadFunc)
{
    binLoadApi.binLoad = (void*(*)(const char*))(loadFunc);
}
#endif

void* CJFileLoader::LoadCJLibrary(const char* libName)
{
    void* handler = binLoadApi.binLoad(libName);
    if (handler != nullptr) {
        std::lock_guard<std::mutex> lock(libCjsoHandlersMutex);
        CString baseName = Os::Path::GetBaseName(libName);
        auto handlerIt =
            std::find_if(cjLibHandlers.begin(), cjLibHandlers.end(), [&baseName](const LibNameToHandler& info) {
                return baseName == Os::Path::GetBaseName(info.baseName.Str());
            });
        if (handlerIt == cjLibHandlers.end()) {
            cjLibHandlers.push_back({ baseName, handler });
        }
    }
    return handler;
}

int CJFileLoader::UnloadLibrary(const char* libName)
{
    if (libName == nullptr) {
        return -1;
    }
    CString baseName = Os::Path::GetBaseName(libName);
    std::lock_guard<std::mutex> lock(libCjsoHandlersMutex);
    auto handlerIt =
        std::find_if(cjLibHandlers.begin(), cjLibHandlers.end(), [&baseName](const LibNameToHandler& info) {
            return baseName == Os::Path::GetBaseName(info.baseName.Str());
        });
    if (handlerIt == cjLibHandlers.end()) {
        return -1;
    }

    int ret = binLoadApi.binUnload(handlerIt->handler);
    if (ret == 0) {
        cjLibHandlers.erase(handlerIt);
    }
    return ret;
}

Uptr CJFileLoader::FindSymbol(const CString libName, const CString symName) const
{
    CString baseName = Os::Path::GetBaseName(libName.Str());
    auto handlerIt =
        std::find_if(cjLibHandlers.begin(), cjLibHandlers.end(), [&baseName](const LibNameToHandler& info) {
            return baseName == Os::Path::GetBaseName(info.baseName.Str());
        });
    if (handlerIt == cjLibHandlers.end()) {
        return 0;
    }
    return reinterpret_cast<Uptr>(binLoadApi.findSymbol(handlerIt->handler, symName.Str()));
}

bool CJFileLoader::DoInitImage(BaseFile* baseFile) const
{
    ScopedEntryTrace trace((CString("CJRT_INIT_LIBRARY_") + baseFile->GetBaseName()).Str());
    std::vector<Uptr> funcs;
    baseFile->GetGlobalInitFunc(funcs);
    for (Uptr func : funcs) {
        if (reinterpret_cast<void*>(func) != nullptr) {
            using FuncType = void (*)();
            FuncType initAddr = reinterpret_cast<FuncType>(func);
#if defined(__OHOS__)
            InitCJLibraryStub(reinterpret_cast<void*>(initAddr));
#else
            Mutator* mutator = ThreadLocal::GetMutator();
            if (mutator != nullptr) {
                mutator->SetManagedContext(true);
            }
            uintptr_t threadData = MapleRuntime::MRT_GetThreadLocalData();
            ExecuteCangjieStub(0, 0, 0, reinterpret_cast<void*>(initAddr), reinterpret_cast<void*>(threadData), 0);
            if (mutator != nullptr) {
                mutator->SetManagedContext(false);
            }
            if (ExceptionManager::HasPendingException()) {
                ExceptionRef ex = ExceptionManager::GetPendingException();
                LOG(RTLOG_ERROR, "Init Image fail! exception occurrence when init image, exception:%s ",
                    ex->GetTypeInfo()->GetName());
                ExceptionManager::ClearPendingException();
                return false;
            }
#endif
        }
    }
    return true;
}

BaseFile* CJFileLoader::GetBaseFile(CString fileName) const
{
    BaseFile* baseFile = nullptr;
    CString baseName = Os::Path::GetBaseName(fileName.Str());
    VisitBaseFile([&baseName, &baseFile](BaseFile* file) {
        if (file->GetBaseName() == baseName) {
            baseFile = file;
            return true;
        } else {
            return false;
        }
    });
    return baseFile;
}

bool CJFileLoader::CheckPackageCompatibility(BaseFile* file)
{
    if (file == nullptr) {
        return false;
    }
#ifdef __arm__
    bool isCompatible = true;
#else
    CString packageName = file->GetRealPath();
    CString packageVersion = file->GetSDKVersion();
    bool isCompatible = compatibility.CheckPackageCompatibility(packageName, packageVersion);
#endif
    file->SetFileCompatibility(isCompatible);
    AddLoadedFiles(file);
    return isCompatible;
}

void CJFileLoader::TryThrowException(Uptr fileMetaAddr)
{
    BaseFile* file = GetBaseFileByMetaAddr(fileMetaAddr);
    if (file == nullptr || file->IsCompatible()) {
        return;
    }
    CString packageName = file->GetRealPath();
    CString packageVersion = file->GetSDKVersion();
    CString msg = "executable cangjie file ";
    msg.Append(packageName);
    msg.Append(CString::FormatString(" version %s is not compatible with deployed cangjie runtime version %s",
        packageVersion.Str(), compatibility.GetRuntimeSDKVersion()));
#ifndef DISABLE_VERSION_CHECK
    ExceptionManager::IncompatiblePackageExpection(msg);
    RemoveLoadedFiles(file);
#else
    LOG(RTLOG_WARNING, "%s", msg.Str());
#endif
}

bool CJFileLoader::IsLazyStaticGI(U32 uuid)
{
    if (lazyInitStaticGIs.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(lazyStaticGIMutex);
    return lazyInitStaticGIs.count(uuid);
}

void CJFileLoader::EraseLazyStaticGI(U32 uuid)
{
    std::lock_guard<std::mutex> lock(lazyStaticGIMutex);
    lazyInitStaticGIs.erase(uuid);
}

U32 CJFileLoader::GetNumOfInterface(TypeInfo* ti)
{
    std::vector<TypeInfo*> itfs;
    ti->GetInterfaces(itfs);
    return itfs.size();
}

TypeInfo* CJFileLoader::GetInterface(TypeInfo* ti, U32 idx)
{
    std::vector<TypeInfo*> itfs;
    ti->GetInterfaces(itfs);
    if (idx >= itfs.size()) {
        return nullptr;
    }
    return itfs[idx];
}
} // namespace MapleRuntime
