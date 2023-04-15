#include "MiniThing.h"
#include "../Utility/Utility.h"

MiniThing::MiniThing(std::wstring volumeName, const char* sqlDBPath)
{
    m_volumeName = volumeName;

    // Set chinese debug output
    std::wcout.imbue(std::locale("chs"));

    // Get folder handle
    if (FAILED(GetHandle()))
    {
        assert(0);
    }

    // Check if folder is ntfs ?
    if (!IsNtfs())
    {
        assert(0);
    }

    // Create or open sql data base file
    if (FAILED(SQLiteOpen(sqlDBPath)))
    {
        assert(0);
    }

    if (! IsSqlExist())
    {
        // Create system usn info
        if (FAILED(CreateUsn()))
        {
            assert(0);
        }

        if (FAILED(QueryUsn()))
        {
            assert(0);
        }

        if (FAILED(RecordUsn()))
        {
            assert(0);
        }

        if (FAILED(SortUsn()))
        {
            assert(0);
        }

        if (FAILED(DeleteUsn()))
        {
            assert(0);
        }
    }

    closeHandle();
}

MiniThing::~MiniThing(VOID)
{
    if (FAILED(SQLiteClose()))
    {
        assert(0);
    }

}

BOOL MiniThing::IsWstringSame(std::wstring s1, std::wstring s2)
{
    CString c1 = s1.c_str();
    CString c2 = s2.c_str();

    return c1.CompareNoCase(c2) == 0 ? TRUE : FALSE;
}

BOOL MiniThing::IsSubStr(std::wstring s1, std::wstring s2)
{
    return (s1.find(s2) != std::wstring::npos);
}

HRESULT MiniThing::GetHandle()
{
    std::wstring folderPath = L"\\\\.\\";
    folderPath += m_volumeName;

    m_hVol = CreateFile(folderPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);

    if (INVALID_HANDLE_VALUE != m_hVol)
    {
        std::wcout << L"Get handle : " << m_volumeName << std::endl;
        return S_OK;
    }
    else
    {
        std::wcout << L"Get handle failed : " << m_volumeName << std::endl;
        return E_FAIL;
    }
}

VOID MiniThing::closeHandle(VOID)
{
    if (TRUE == CloseHandle(m_hVol))
    {
        std::wcout << L"Close handle : " << m_volumeName << std::endl;
    }
    else
    {
        std::wcout << L"Get handle : " << m_volumeName << std::endl;
    }

    m_hVol = INVALID_HANDLE_VALUE;
}

VOID MiniThing::GetSystemError(VOID)
{
    LPCTSTR   lpMsgBuf;
    DWORD lastError = GetLastError();
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        lastError,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf,
        0,
        NULL
    );

    LPCTSTR strValue = lpMsgBuf;
    _tprintf(_T("ERROR msg : %s\n"), strValue);
}

BOOL MiniThing::IsNtfs(VOID)
{
    BOOL isNtfs = FALSE;
    char sysNameBuf[MAX_PATH] = { 0 };

    int len = WstringToChar(m_volumeName + L"\\", nullptr);
    char* pVol = new char[len];
    WstringToChar(m_volumeName + L"\\", pVol);

    BOOL status = GetVolumeInformationA(
        pVol,
        NULL,
        0,
        NULL,
        NULL,
        NULL,
        sysNameBuf,
        MAX_PATH);

    if (FALSE != status)
    {
        std::cout << "File system name : " << sysNameBuf << std::endl;

        if (0 == strcmp(sysNameBuf, "NTFS"))
        {
            isNtfs = true;
        }
        else
        {
            std::cout << "File system not NTFS format !!!" << std::endl;
            GetSystemError();
        }
    }

    return isNtfs;
}

HRESULT MiniThing::CreateUsn(VOID)
{
    HRESULT ret = S_OK;

    DWORD br;
    CREATE_USN_JOURNAL_DATA cujd;
    cujd.MaximumSize = 0;
    cujd.AllocationDelta = 0;
    BOOL status = DeviceIoControl(
        m_hVol,
        FSCTL_CREATE_USN_JOURNAL,
        &cujd,
        sizeof(cujd),
        NULL,
        0,
        &br,
        NULL);

    if (FALSE != status)
    {
        std::cout << "Create usn file success" << std::endl;
        ret = S_OK;
    }
    else
    {
        std::cout << "Create usn file failed" << std::endl;
        GetSystemError();
        ret = E_FAIL;
    }

    return ret;
}

HRESULT MiniThing::QueryUsn(VOID)
{
    HRESULT ret = S_OK;

    DWORD br;
    BOOL status = DeviceIoControl(m_hVol,
        FSCTL_QUERY_USN_JOURNAL,
        NULL,
        0,
        &m_usnInfo,
        sizeof(m_usnInfo),
        &br,
        NULL);

    if (FALSE != status)
    {
        std::cout << "Query usn info success" << std::endl;
    }
    else
    {
        ret = E_FAIL;
        std::cout << "Query usn info failed" << std::endl;
        GetSystemError();
    }

    return ret;
}

HRESULT MiniThing::RecordUsn(VOID)
{
    MFT_ENUM_DATA med = { 0, 0, m_usnInfo.NextUsn };
    med.MaxMajorVersion = 2;
    // Used to record usn info, must big enough
    char buffer[0x1000];
    DWORD usnDataSize = 0;
    PUSN_RECORD pUsnRecord;

    // Find the first USN record
    // return a USN followed by zero or more change journal records, each in a USN_RECORD structure
    while (FALSE != DeviceIoControl(m_hVol,
        FSCTL_ENUM_USN_DATA,
        &med,
        sizeof(med),
        buffer,
        _countof(buffer),
        &usnDataSize,
        NULL))
    {
        DWORD dwRetBytes = usnDataSize - sizeof(USN);
        pUsnRecord = (PUSN_RECORD)(((PCHAR)buffer) + sizeof(USN));
        DWORD cnt = 0;

        while (dwRetBytes > 0)
        {
            // Here FileNameLength may count in bytes, and each wchar_t occupy 2 bytes
            wchar_t* pWchar = new wchar_t[pUsnRecord->FileNameLength / 2 + 1];
            memcpy(pWchar, pUsnRecord->FileName, pUsnRecord->FileNameLength);
            pWchar[pUsnRecord->FileNameLength / 2] = 0x00;
            // wcsncpy_s(pWchar, pUsnRecord->FileNameLength / 2, pUsnRecord->FileName, pUsnRecord->FileNameLength / 2);
            std::wstring fileNameWstr = WcharToWstring(pWchar);
            std::string fileNameStr = WstringToString(fileNameWstr);
            delete pWchar;

            UsnInfo usnInfo = { 0 };
            usnInfo.fileNameStr = fileNameStr;
            usnInfo.fileNameWstr = fileNameWstr;
            usnInfo.pParentRef = pUsnRecord->ParentFileReferenceNumber;
            usnInfo.pSelfRef = pUsnRecord->FileReferenceNumber;
            usnInfo.timeStamp = pUsnRecord->TimeStamp;

            m_usnRecordMap[usnInfo.pSelfRef] = usnInfo;

            // Get the next USN record
            DWORD recordLen = pUsnRecord->RecordLength;
            dwRetBytes -= recordLen;
            pUsnRecord = (PUSN_RECORD)(((PCHAR)pUsnRecord) + recordLen);
        }

        // Get next page USN record
        // from MSDN(http://msdn.microsoft.com/en-us/library/aa365736%28v=VS.85%29.aspx ):  
        // The USN returned as the first item in the output buffer is the USN of the next record number to be retrieved.  
        // Use this value to continue reading records from the end boundary forward.  
        med.StartFileReferenceNumber = *(USN*)&buffer;
    }

    return S_OK;
}

VOID MiniThing::GetCurrentFilePath(std::wstring& path, DWORDLONG currentRef, DWORDLONG rootRef)
{
    // 1. This is root node, just add root path and return
    if (currentRef == rootRef)
    {
        path = m_volumeName + L"\\" + path;
        return;
    }

    if (m_usnRecordMap.find(currentRef) != m_usnRecordMap.end())
    {
        // 2. Normal node, loop more
        std::wstring str = m_usnRecordMap[currentRef].fileNameWstr;
        path = str + L"\\" + path;
        GetCurrentFilePath(path, m_usnRecordMap[currentRef].pParentRef, rootRef);
    }
    else
    {
        // 3. Some system files's root node is not in current folder
        std::wstring str = L"?";
        path = str + L"\\" + path;

        return;
    }
}

VOID GetCurrentFilePathV2(std::wstring& path, std::wstring volName, DWORDLONG currentRef, DWORDLONG rootRef, unordered_map<DWORDLONG, UsnInfo>& recordMapAll)
{
    // 1. This is root node, just add root path and return
    if (currentRef == rootRef)
    {
        path = volName + L"\\" + path;
        return;
    }

    if (recordMapAll.find(currentRef) != recordMapAll.end())
    {
        // 2. Normal node, loop more
        std::wstring str = recordMapAll[currentRef].fileNameWstr;
        path = str + L"\\" + path;
        GetCurrentFilePathV2(path, volName, recordMapAll[currentRef].pParentRef, rootRef, recordMapAll);
    }
    else
    {
        // 3. Some system files's root node is not in current folder
        std::wstring str = L"?";
        path = str + L"\\" + path;

        return;
    }
}

DWORD WINAPI SortThread(LPVOID lp)
{
    SortTaskInfo* pTaskInfo = (SortTaskInfo*)lp;

    // Set chinese debug output
    std::wcout.imbue(std::locale("chs"));
    setlocale(LC_ALL, "zh-CN");

    printf("Sort thread start\n");

    do
    {
        LARGE_INTEGER timeStart;
        LARGE_INTEGER timeEnd;
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        double quadpart = (double)frequency.QuadPart;
        QueryPerformanceCounter(&timeStart);

        for (auto it = (*pTaskInfo->pSortTask).begin(); it != (*pTaskInfo->pSortTask).end(); it++)
        {
            UsnInfo usnInfo = it->second;
            std::wstring path(usnInfo.fileNameWstr);
            GetCurrentFilePathV2(path, L"F:", usnInfo.pSelfRef, pTaskInfo->rootRef, *(pTaskInfo->pAllUsnRecordMap));

            (*pTaskInfo->pSortTask)[it->first].filePathWstr = path;
        }

        QueryPerformanceCounter(&timeEnd);
        double elapsed = (timeEnd.QuadPart - timeStart.QuadPart) / quadpart;
        std::cout << "Sort task "<< pTaskInfo->taskIndex <<" over : " << elapsed << " S" << std::endl;
    } while (0);

    printf("Query thread stop\n");
    return 0;
}

HRESULT MiniThing::SortUsn(VOID)
{
    HRESULT ret = S_OK;

    std::cout << "Generating sql data base......" << std::endl;

    // 1. Get root file node
    //  cause "System Volume Information" is a system file and just under root folder
    //  so we use it to find root folder
    std::wstring cmpStr(L"System Volume Information");
    m_rootFileNode = 0x0;

    for (auto it = m_usnRecordMap.begin(); it != m_usnRecordMap.end(); it++)
    {
        UsnInfo usnInfo = it->second;
        if (0 == usnInfo.fileNameWstr.compare(cmpStr))
        {
            m_rootFileNode = usnInfo.pParentRef;
            break;
        }
    }

    if (m_rootFileNode == 0)
    {
        std::cout << "Cannot find root folder" << std::endl;
        ret = E_FAIL;
        assert(0);
    }

    // 2. Divide file node into several sort tasks
    int fileNodeCnt = 0;
    vector<unordered_map<DWORDLONG, UsnInfo>> sortTaskSet;
    unordered_map<DWORDLONG, UsnInfo> mapTmp;

    for (auto it = m_usnRecordMap.begin(); it != m_usnRecordMap.end(); it++)
    {
        UsnInfo usnInfo = it->second;
        mapTmp[it->first] = it->second;
        ++fileNodeCnt;

        if (fileNodeCnt % SORT_TASK_GRANULARITY == 0)
        {
            sortTaskSet.push_back(mapTmp);
            mapTmp.clear();
        }
        else
        {
            continue;
        }
    }
    if (!mapTmp.empty())
    {
        fileNodeCnt += mapTmp.size();
        sortTaskSet.push_back(mapTmp);
        mapTmp.clear();
    }

    vector<HANDLE> taskHandleVec;
    vector<SortTaskInfo> sortTaskVec;

    for (int i = 0; i < sortTaskSet.size(); i++)
    {
        SortTaskInfo taskInfo;
        taskInfo.taskIndex = i;
        taskInfo.pSortTask = &(sortTaskSet[i]);
        taskInfo.pAllUsnRecordMap = &m_usnRecordMap;
        taskInfo.rootRef = m_rootFileNode;

        sortTaskVec.push_back(taskInfo);
    }

    // 3. Execute all sort tasks by threads
    for (int i = 0; i < sortTaskSet.size(); i++)
    {
        HANDLE taskThread = CreateThread(0, 0, SortThread, &(sortTaskVec[i]), CREATE_SUSPENDED, 0);
        if (taskThread)
        {
            ResumeThread(taskThread);
            taskHandleVec.push_back(taskThread);
        }
        else
        {
            assert(0);
        }
    }

    // 4. Wait all sort thread over
    for (auto it = taskHandleVec.begin(); it != taskHandleVec.end(); it++)
    {
        DWORD dwWaitCode = WaitForSingleObject(*it, INFINITE);
        assert(dwWaitCode == WAIT_OBJECT_0);
        CloseHandle(*it);
    }

    taskHandleVec.clear();
    sortTaskVec.clear();

    // 5. Get all sort results
    for (auto it = sortTaskSet.begin(); it != sortTaskSet.end(); it++)
    {
        auto tmp = *it;
        for (auto it1 = tmp.begin(); it1 != tmp.end(); it1++)
        {
            UsnInfo usnInfo = it1->second;
            SQLiteInsert(&usnInfo);
#if _DEBUG
            // Print file node info
            // std::wcout << usnInfo.filePathWstr << std::endl;
#endif
        }
    }

    // After file node sorted, the map can be destroyed
    sortTaskSet.clear();
    m_usnRecordMap.clear();

    std::wcout << "Sort file node sum : " << fileNodeCnt << std::endl;

    return ret;
}

HRESULT MiniThing::DeleteUsn(VOID)
{
    HRESULT ret = S_OK;

    DELETE_USN_JOURNAL_DATA dujd;
    dujd.UsnJournalID = m_usnInfo.UsnJournalID;
    dujd.DeleteFlags = USN_DELETE_FLAG_DELETE;
    DWORD lenRet = 0;

    int status = DeviceIoControl(m_hVol,
        FSCTL_DELETE_USN_JOURNAL,
        &dujd,
        sizeof(dujd),
        NULL,
        0,
        &lenRet,
        NULL);

    if (FALSE != status)
    {
        std::cout << "Delete usn file success" << std::endl;
    }
    else
    {
        GetSystemError();
        ret = E_FAIL;
        std::cout << "Delete usn file failed" << std::endl;
    }

    return ret;
}

std::wstring MiniThing::GetFileNameAccordPath(std::wstring path)
{
    return path.substr(path.find_last_of(L"\\") + 1);
}

std::wstring MiniThing::GetPathAccordPath(std::wstring path)
{
    wstring name = GetFileNameAccordPath(path);
    return path.substr(0, path.length() - name.length());
}

VOID MiniThing::AdjustUsnRecord(std::wstring folder, std::wstring filePath, std::wstring reFileName, DWORD op)
{
    switch (op)
    {
    case FILE_ACTION_ADDED:
    {
        UsnInfo tmp = { 0 };
        tmp.filePathWstr = filePath;
        tmp.fileNameWstr = GetFileNameAccordPath(filePath);
        tmp.pParentRef = m_constFileRefNumMax;
        tmp.pSelfRef = m_unusedFileRefNum--;

        // Update sql db
        if (FAILED(SQLiteInsert(&tmp)))
        {
            assert(0);
        }
        break;
    }

    case FILE_ACTION_RENAMED_OLD_NAME:
    {
        // Update sql db
        // TODO:
        //      if rename a folder?
        UsnInfo usnInfo = { 0 };
        usnInfo.fileNameWstr = reFileName;
        if (FAILED(SQLiteUpdate(&usnInfo, filePath)))
        {
            assert(0);
        }
        break;
    }

    case FILE_ACTION_REMOVED:
    {
        // Update sql db
        // TODO:
        //      if remove a folder?
        UsnInfo tmpUsn = { 0 };
        tmpUsn.filePathWstr = filePath;
        if (FAILED(SQLiteDelete(&tmpUsn)))
        {
            assert(0);
        }
        break;
    }

    default:
        assert(0);
    }
}

DWORD WINAPI MonitorThread(LPVOID lp)
{
    MiniThing* pMiniThing = (MiniThing*)lp;

    char notifyInfo[1024];

    wstring fileNameWstr;
    wstring fileRenameWstr;

    // Set chinese debug output
    std::wcout.imbue(std::locale("chs"));
    setlocale(LC_ALL, "zh-CN");

    std::wcout << L"Start monitor : " << pMiniThing->GetVolName() << std::endl;

    std::wstring folderPath = pMiniThing->GetVolName();

    HANDLE dirHandle = CreateFile(folderPath.c_str(),
        GENERIC_READ | GENERIC_WRITE | FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);

    // 若网络重定向或目标文件系统不支持该操作，函数失败，同时调用GetLastError()返回ERROR_INVALID_FUNCTION
    if (dirHandle == INVALID_HANDLE_VALUE)
    {
        std::cout << "Error " << GetLastError() << std::endl;
        assert(0);
    }

    auto* pNotifyInfo = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(notifyInfo);

    while (true)
    {
        // Check if need exit thread
        DWORD dwWaitCode = WaitForSingleObject(pMiniThing->m_hExitEvent, 0x0);
        if (WAIT_OBJECT_0 == dwWaitCode)
        {
            std::cout << "Recv the quit event" << std::endl;
            break;
        }

        DWORD retBytes;

        if (ReadDirectoryChangesW(
            dirHandle,
            &notifyInfo,
            sizeof(notifyInfo),
            true, // Monitor sub directory
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE,
            &retBytes,
            nullptr,
            nullptr))
        {
            // Change file name
            fileNameWstr = pNotifyInfo->FileName;
            fileNameWstr[pNotifyInfo->FileNameLength / 2] = 0;

            // Rename file new name
            auto pInfo = reinterpret_cast<PFILE_NOTIFY_INFORMATION>(reinterpret_cast<char*>(pNotifyInfo) + pNotifyInfo->NextEntryOffset);
            fileRenameWstr = pInfo->FileName;
            fileRenameWstr[pInfo->FileNameLength / 2] = 0;

            switch (pNotifyInfo->Action)
            {
            case FILE_ACTION_ADDED:
                if (fileNameWstr.find(L"$RECYCLE.BIN") == wstring::npos)
                {
                    std::wstring addPath;
                    addPath.clear();
                    addPath.append(pMiniThing->GetVolName());
                    addPath.append(L"\\");
                    addPath.append(fileNameWstr);

                    // Here use printf to suit multi-thread
                    wprintf(L"\nAdd file : %s\n", addPath.c_str());

                    pMiniThing->AdjustUsnRecord(pMiniThing->GetVolName(), addPath, L"", FILE_ACTION_ADDED);
                }
                break;

            case FILE_ACTION_MODIFIED:
                if (fileNameWstr.find(L"$RECYCLE.BIN") == wstring::npos &&
                    fileNameWstr.find(L"fileAdded.txt") == wstring::npos &&
                    fileNameWstr.find(L"fileRemoved.txt") == wstring::npos)
                {
                    std::wstring modPath;
                    modPath.append(pMiniThing->GetVolName());
                    modPath.append(L"\\");
                    modPath.append(fileNameWstr);
                    // Here use printf to suit multi-thread
                    wprintf(L"Mod file : %s\n", modPath.c_str());
                    //add_record(to_utf8(StringToWString(data)));
                }
                break;

            case FILE_ACTION_REMOVED:
                if (fileNameWstr.find(L"$RECYCLE.BIN") == wstring::npos)
                {
                    std::wstring remPath;
                    remPath.clear();
                    remPath.append(pMiniThing->GetVolName());
                    remPath.append(L"\\");
                    remPath.append(fileNameWstr);

                    // Here use printf to suit multi-thread
                    wprintf(L"\nRemove file : %s\n", remPath.c_str());

                    pMiniThing->AdjustUsnRecord(pMiniThing->GetVolName(), remPath, L"", FILE_ACTION_REMOVED);
                }
                break;

            case FILE_ACTION_RENAMED_OLD_NAME:
                if (fileNameWstr.find(L"$RECYCLE.BIN") == wstring::npos)
                {
                    wstring oriName;
                    oriName.clear();
                    oriName.append(pMiniThing->GetVolName());
                    oriName.append(L"\\");
                    oriName.append(fileNameWstr);

                    wstring reName;
                    reName.clear();
                    reName.append(pMiniThing->GetVolName());
                    reName.append(L"\\");
                    reName.append(fileRenameWstr);

                    // Here use printf to suit multi-thread
                    wprintf(L"\nRename file : %s -> %s\n", oriName.c_str(), reName.c_str());

                    pMiniThing->AdjustUsnRecord(pMiniThing->GetVolName(), oriName, reName, FILE_ACTION_RENAMED_OLD_NAME);
                }
                break;

            default:
                std::wcout << L"Unknown command" << std::endl;
            }
        }
    }

    CloseHandle(dirHandle);

    std::cout << "Monitor thread stop" << std::endl;

    return 0;
}

HRESULT MiniThing::CreateMonitorThread(VOID)
{
    HRESULT ret = S_OK;

    m_hExitEvent = CreateEvent(0, 0, 0, 0);

    // 以挂起方式创建线程
    m_hMonitorThread = CreateThread(0, 0, MonitorThread, this, CREATE_SUSPENDED, 0);
    if (INVALID_HANDLE_VALUE == m_hMonitorThread)
    {
        GetSystemError();
        ret = E_FAIL;
    }

    return ret;
}

VOID MiniThing::StartMonitorThread(VOID)
{
    // 使线程开始运行
    ResumeThread(m_hMonitorThread);
}

VOID MiniThing::StopMonitorThread(VOID)
{
    // 使事件处于激发的状态
    SetEvent(m_hExitEvent);

    // 等待线程运行结束
    DWORD dwWaitCode = WaitForSingleObject(m_hMonitorThread, INFINITE);

    // 断言判断线程是否正常结束
    assert(dwWaitCode == WAIT_OBJECT_0);

    // 释放线程句柄
    CloseHandle(m_hMonitorThread);
}

DWORD WINAPI QueryThread(LPVOID lp)
{
    MiniThing* pMiniThing = (MiniThing*)lp;

    // Set chinese debug output
    std::wcout.imbue(std::locale("chs"));
    setlocale(LC_ALL, "zh-CN");

    printf("Query thread start\n");

    while (TRUE)
    {
        std::wcout << std::endl << L"==============================" << std::endl;
        std::wcout << L"Input query file info here:" << std::endl;

        std::wstring query;
        std::wcin >> query;

        std::vector<std::wstring> vec;

        LARGE_INTEGER timeStart;
        LARGE_INTEGER timeEnd;
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        double quadpart = (double)frequency.QuadPart;

        QueryPerformanceCounter(&timeStart);

        pMiniThing->SQLiteQuery(query, vec);

        QueryPerformanceCounter(&timeEnd);
        double elapsed = (timeEnd.QuadPart - timeStart.QuadPart) / quadpart;
        std::cout << "Time elapsed : " << elapsed << " S" << std::endl;

        if (vec.empty())
        {
            std::wcout << "Not found" << std::endl;
        }
        else
        {
            int cnt = 0;
            for (auto it = vec.begin(); it != vec.end(); it++)
            {
                std::wcout << L"No." << cnt++ << " : " << *it << endl;
            }
        }
        std::wcout << L"==============================" << std::endl;
    }

    printf("Query thread stop\n");
    return 0;
}

HRESULT MiniThing::CreateQueryThread(VOID)
{
    HRESULT ret = S_OK;

    m_hQueryExitEvent = CreateEvent(0, 0, 0, 0);

    // 以挂起方式创建线程
    m_hQueryThread = CreateThread(0, 0, QueryThread, this, CREATE_SUSPENDED, 0);
    if (INVALID_HANDLE_VALUE == m_hQueryThread)
    {
        GetSystemError();
        ret = E_FAIL;
    }

    return ret;
}

VOID MiniThing::StartQueryThread(VOID)
{
    // 使线程开始运行
    ResumeThread(m_hQueryThread);
}

VOID MiniThing::StopQueryThread(VOID)
{
    // 使事件处于激发的状态
    SetEvent(m_hQueryExitEvent);

    // 等待线程运行结束
    DWORD dwWaitCode = WaitForSingleObject(m_hQueryThread, INFINITE);

    // 断言判断线程是否正常结束
    assert(dwWaitCode == WAIT_OBJECT_0);

    // 释放线程句柄
    CloseHandle(m_hQueryThread);
}

HRESULT MiniThing::SQLiteOpen(CONST CHAR* path)
{
    HRESULT ret = S_OK;
    m_SQLitePath = path;
    int result = sqlite3_open_v2(path, &m_hSQLite, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_SHAREDCACHE, NULL);

    if (result == SQLITE_OK)
    {
        char* errMsg = nullptr;

        std::string create;
        create.append("CREATE TABLE UsnInfo(SelfRef sqlite_uint64, ParentRef sqlite_uint64, TimeStamp sqlite_int64, FileName TEXT, FilePath TEXT);");

        int res = sqlite3_exec(m_hSQLite, create.c_str(), 0, 0, &errMsg);
        if (res == SQLITE_OK)
        {
            m_isSqlExist = FALSE;
        }
        else
        {
            std::cout << "SQLite : data base already exist" << std::endl;
            m_isSqlExist = TRUE;
        }

        std::cout << "SQLite : open success" << std::endl;
    }
    else
    {
        ret = E_FAIL;
        std::cout << "SQLite : open failed" << std::endl;
    }

    return ret;
}

HRESULT MiniThing::SQLiteInsert(UsnInfo* pUsnInfo)
{
    HRESULT ret = S_OK;

    // "CREATE TABLE UsnInfo(SelfRef sqlite_uint64, ParentRef sqlite_uint64, TimeStamp sqlite_int64, FileName TEXT, FilePath TEXT);"
    char sql[1024] = { 0 };
    std::string nameUtf8 = UnicodeToUtf8(pUsnInfo->fileNameWstr);
    std::string pathUtf8 = UnicodeToUtf8(pUsnInfo->filePathWstr);
    sprintf_s(sql, "INSERT INTO UsnInfo VALUES(%llu, %llu, %lld, '%s', '%s');",
        pUsnInfo->pSelfRef, pUsnInfo->pParentRef, pUsnInfo->timeStamp, nameUtf8.c_str(), pathUtf8.c_str());

    char* errMsg = nullptr;
    if (sqlite3_exec(m_hSQLite, sql, NULL, NULL, &errMsg) != SQLITE_OK)
    {
        ret = E_FAIL;
        printf("sqlite : insert failed\n");
        printf("error : %s\n", errMsg);
    }
    else
    {
        // printf("sqlite : insert done\n");
    }

    return ret;
}

HRESULT MiniThing::SQLiteQuery(std::wstring queryInfo, std::vector<std::wstring>& vec)
{
    HRESULT ret = S_OK;
    char sql[1024] = { 0 };
    char* errMsg = nullptr;

    // 1. Conveter unicode -> utf-8
    std::string str = UnicodeToUtf8(queryInfo);

    // 2. Query and pop vector
    sprintf_s(sql, "SELECT * FROM UsnInfo WHERE FileName LIKE '%%%s%%';", str.c_str());

    sqlite3_stmt* stmt = NULL;
    int res = sqlite3_prepare_v2(m_hSQLite, sql, (int)strlen(sql), &stmt, NULL);
    if (SQLITE_OK == res && NULL != stmt)
    {
        // "CREATE TABLE UsnInfo(SelfRef sqlite_uint64, ParentRef sqlite_uint64, TimeStamp sqlite_int64, FileName TEXT, FilePath TEXT);"
        while (SQLITE_ROW == sqlite3_step(stmt))
        {
            const unsigned char* pName = sqlite3_column_text(stmt, 3);
            const unsigned char* pPath = sqlite3_column_text(stmt, 4);
            std::string strName = (char*)pName;
            std::string strPath = (char*)pPath;

            std::wstring wstrName = Utf8ToUnicode(strName);
            std::wstring wstrPath = Utf8ToUnicode(strPath);

            vec.push_back(wstrPath);
        }
    }
    else
    {
        ret = E_FAIL;
        printf("sqlite : query failed\n");
    }

    sqlite3_finalize(stmt);

    return ret;
}

HRESULT MiniThing::SQLiteQueryV2(QueryInfo* queryInfo, std::vector<UsnInfo>& vec)
{
    HRESULT ret = S_OK;
    char sql[1024] = { 0 };
    char* errMsg = nullptr;

    if (queryInfo)
    {
        switch (queryInfo->type)
        {
        case BY_REF:
        {
            sprintf_s(sql, "SELECT * FROM UsnInfo WHERE SelfRef = %llu;", queryInfo->info.pSelfRef);
            break;
        }
        case BY_NAME:
        {
            std::string str = UnicodeToUtf8(queryInfo->info.fileNameWstr);
            sprintf_s(sql, "SELECT * FROM UsnInfo WHERE FileName LIKE '%%%s%%';", str.c_str());
            break;
        }
        default:
            assert(0);
            break;
        }
    }
    else
    {
        sprintf_s(sql, "SELECT * FROM UsnInfo;");
    }

    sqlite3_stmt* stmt = NULL;
    int res = sqlite3_prepare_v2(m_hSQLite, sql, (int)strlen(sql), &stmt, NULL);
    if (SQLITE_OK == res && NULL != stmt)
    {
        // "CREATE TABLE UsnInfo(SelfRef sqlite_uint64, ParentRef sqlite_uint64, TimeStamp sqlite_int64, FileName TEXT, FilePath TEXT);"
        while (SQLITE_ROW == sqlite3_step(stmt))
        {
            DWORDLONG self = sqlite3_column_int64(stmt, 0);
            DWORDLONG parent = sqlite3_column_int64(stmt, 1);
            const unsigned char* pName = sqlite3_column_text(stmt, 3);
            const unsigned char* pPath = sqlite3_column_text(stmt, 4);
            std::string strName = (char*)pName;
            std::string strPath = (char*)pPath;

            std::wstring wstrName = Utf8ToUnicode(strName);
            std::wstring wstrPath = Utf8ToUnicode(strPath);

            UsnInfo usnInfo = { 0 };
            usnInfo.pSelfRef = self;
            usnInfo.pParentRef = parent;
            usnInfo.fileNameWstr = wstrName;
            usnInfo.filePathWstr = wstrPath;

            vec.push_back(usnInfo);
        }
    }
    else
    {
        ret = E_FAIL;
        printf("sqlite : query failed\n");
    }

    sqlite3_finalize(stmt);

    return ret;
}

HRESULT MiniThing::SQLiteDelete(UsnInfo* pUsnInfo)
{
    HRESULT ret = S_OK;

    // Update sql db
    char sql[1024] = { 0 };
    char* errMsg = nullptr;

    // "CREATE TABLE UsnInfo(SelfRef sqlite_uint64, ParentRef sqlite_uint64, TimeStamp sqlite_int64, FileName TEXT, FilePath TEXT);"
    std::string path = UnicodeToUtf8(pUsnInfo->filePathWstr);
    sprintf_s(sql, "DELETE FROM UsnInfo WHERE FilePath = '%s';", path.c_str());
    if (sqlite3_exec(m_hSQLite, sql, NULL, NULL, &errMsg) != SQLITE_OK)
    {
        ret = E_FAIL;
        printf("sqlite : delete failed\n");
        printf("error : %s\n", errMsg);
    }
    else
    {
        // printf("sqlite : delete done\n");
    }

    return ret;
}

HRESULT MiniThing::SQLiteUpdate(UsnInfo* pUsnInfo, std::wstring originPath)
{
    HRESULT ret = S_OK;

    char sql[1024] = { 0 };
    char* errMsg = nullptr;

    // "CREATE TABLE UsnInfo(SelfRef sqlite_uint64, ParentRef sqlite_uint64, TimeStamp sqlite_int64, FileName TEXT, FilePath TEXT);"
    std::string oriPath = UnicodeToUtf8(originPath);
    std::string newPath = UnicodeToUtf8(pUsnInfo->filePathWstr);
    std::string newName = UnicodeToUtf8(pUsnInfo->fileNameWstr);
    sprintf_s(sql, "UPDATE UsnInfo SET FileName = '%s', FilePath = '%s' WHERE FilePath = '%s';", newName.c_str(), newPath.c_str(), oriPath.c_str());
    if (sqlite3_exec(m_hSQLite, sql, NULL, NULL, &errMsg) != SQLITE_OK)
    {
        ret = E_FAIL;
        printf("sqlite : update failed\n");
        printf("error : %s\n", errMsg);
    }
    else
    {
        // printf("sqlite : update done\n");
    }

    return ret;
}

HRESULT MiniThing::SQLiteUpdateV2(UsnInfo* pUsnInfo, DWORDLONG selfRef)
{
    HRESULT ret = S_OK;

    char sql[1024] = { 0 };
    char* errMsg = nullptr;

    // "CREATE TABLE UsnInfo(SelfRef sqlite_uint64, ParentRef sqlite_uint64, TimeStamp sqlite_int64, FileName TEXT, FilePath TEXT);"
    std::string newPath = UnicodeToUtf8(pUsnInfo->filePathWstr);
    sprintf_s(sql, "UPDATE UsnInfo SET FilePath = '%s' WHERE SelfRef = %llu;", newPath.c_str(), selfRef);
    if (sqlite3_exec(m_hSQLite, sql, NULL, NULL, &errMsg) != SQLITE_OK)
    {
        ret = E_FAIL;
        printf("sqlite : update failed\n");
        printf("error : %s\n", errMsg);
    }
    else
    {
        // printf("sqlite : update done\n");
    }

    return ret;
}

HRESULT MiniThing::SQLiteClose(VOID)
{
    sqlite3_close_v2(m_hSQLite);

    return S_OK;
}

