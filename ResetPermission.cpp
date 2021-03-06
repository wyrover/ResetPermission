/*-------------------------------------------------------------------------
Reset files permission (c) Elias Bachaalany

lallousz-x86@yahoo.com


History
---------

08/24/2013 - Initial version
08/30/2013 - Enclose the folder with quotes if it contains at least one space character
09/17/2013 - Added "Reset files permission" as a optional action
           - Added "Reset hidden and system files"
03/31/2014 - Fixed double backslash when folder is root
01/08/2014 - Added "Do not follow symbolic links" option
03/31/2015 - Allow editing of the generated command textbox
           - Added "More actions" to add Explorer shell context menu
11/03/2015 - Added /SKIPSL switch to takeown.exe

11/15/2015 - v1.1.3
           - Added HELP button to redirect to blog entry
           - Added warning when attempting to change permission of a root folder

02/13/2016 - v1.1.4
           - Minor code changes
           - Update the console window title when the commands execute

02/28/2016 - v1.1.5
           - Refactored code
           - Added the Advanced button
           - Added Backup/Restore permissions

06/14/2016 - v1.1.6
           - Made the dialog non-static
           - Refactored the code
           - bugfix: Add/Remove from the Explorer folder context menu did not work unless a folder was selected.
                     Fix: No folder selection is needed for that action.

12/19/2016 - v1.1.7
           - Attempt to make ResetPermission AntiVirus false-positive free by using 
             local app data folder instead of temp and by not deleting the temp batch script

04/30/2017 - v1.2.0
           - Browsing a folder will remember and focus on the previous folder
           - Support unicode path
           - Warn if the user is trying to reset permissions on a non-supported (or ACL-able) file system
           - Fix: command line parsing was erroneous
           - Fix: invoking the tool from the shell context menu was failing if the path contained space characters

-------------------------------------------------------------------------*/

#include "stdafx.h"

//-------------------------------------------------------------------------
static LPCTSTR STR_HELP_URL          = _TEXT("http://lallouslab.net/2013/08/26/resetting-ntfs-files-permission-in-windows-graphical-utility/");
static LPCTSTR STR_SELECT_FOLDER     = _TEXT("Please select a folder");
static LPCTSTR STR_ERROR             = _TEXT("Error");
static LPCTSTR STR_CONFIRMATION      = _TEXT("Confirmation");
static LPCTSTR STR_RESET_FN          = _TEXT("resetperm.bat");
static LPCTSTR STR_HKCR_CTXMENU_BASE = _TEXT("\"HKCR\\Folder\\shell\\Reset Permission");
static LPCTSTR STR_HKCR_CTXMENU_CMD  = _TEXT("\\command");
static stringT STR_CMD_PAUSE         = _TEXT("pause\r\n");
static LPCTSTR STR_CMD_ICACLS        = _TEXT("icacls ");
static LPCTSTR STR_CMD_TAKEOWN       = _TEXT("takeown");
static LPCTSTR STR_CMD_ATTRIB        = _TEXT("attrib");
static LPCTSTR STR_FOLDER_LALLOUSLAB = _TEXT("\\lallouslab");
static LPCTSTR STR_CMD_REG           = _TEXT("reg");
static stringT STR_NEWLINE           = _TEXT("\r\n");
static stringT STR_NEWLINE2          = STR_NEWLINE + STR_NEWLINE;

static LPCTSTR STR_WARNING           = _TEXT("Warning!");

static LPCTSTR STR_FS_NOT_SUPPORTED_WARNING =
        _TEXT("The selected path does not support file permissions and thus using this tool might not have any effects!\n\n")
        _TEXT("Are you sure you want to continue?");

static LPCTSTR STR_ROOT_WARNING      =
        _TEXT("You are about to change the permission of a root folder!\n")
        _TEXT("This is a **DANGEROUS** operation! It is better to choose a specific folder instead!\n\n")
        _TEXT("!! If you choose to proceed then you might render your system unstable !!\n\n")
        _TEXT("Are you sure you want to continue?");

static LPCTSTR STR_ADDREM_CTXMENU_CONFIRM =
        _TEXT("You are about to add or remove the ResetPermission tool to/from the Windows Explorer folder context menu!\n")
        _TEXT("\n")
        _TEXT("Are you sure you want to continue?");

static LPCTSTR STR_TITLE_BACKUP_PERMS = 
        _TEXT("Pick the file you wish to backup the permissions into");

static LPCTSTR STR_TITLE_RESTORE_PERMS = 
        _TEXT("Pick the permissions backup file you wish to restore from");

static LPCTSTR STR_CHECK_THE_BATCHOGRAPHY_BOOK =
        _TEXT("REM -- Check out the book: Batchography - The Art of Batch Files Programming\r\n")
        _TEXT("REM -- http://lallouslab.net/2016/05/10/batchography/\r\n")
        _TEXT("\r\n");

//-------------------------------------------------------------------------
static bool IsSupportedFileSystem(LPCTSTR Path)
{
    do 
    {
        // Get the root path only
        TCHAR RootPath[4];
        _tcsncpy_s(RootPath, Path, _countof(RootPath) - 1);
        if (_tcslen(RootPath) < 3)
            break;

        // Get the file system name
        TCHAR FileSysName[MAX_PATH + 1];
        if (!GetVolumeInformation(
            RootPath,
            nullptr,
            0,
            nullptr,
            nullptr,
            nullptr,
            FileSysName,
            _countof(FileSysName)))
        {
            break;
        }

        // Compare against supported file systems
        return _tcscmp(FileSysName, _TEXT("NTFS")) == 0;
    } while (false);

    // If we fail to determinte the FS type, then assume we support it and
    // let the user decide what to do
    return true;
}

//-------------------------------------------------------------------------
// The premise behind this function is that if we can convert a UTF-16
// string to both UTF-8 and ASCII and they are equal then no encoding is required
static bool ConvertUtf16ToUtf8(
    const wchar_t *utf16_str,
    bool *bEncodingRequired,
    std::string *utf8_str)
{
    size_t utf16_len = wcslen(utf16_str);

    int utf8_size = WideCharToMultiByte(
        CP_UTF8,
        0,
        utf16_str,
        utf16_len,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (utf8_size == 0)
        return false;

    // If the UTF8 string's length is the same as the the UTF16 length
    // then it means we did not need extra bytes to encode some UTF8 characters
    // that are not part of the ASCII set
    bool bNeedEncoding = utf8_size != utf16_len;

    if (utf8_str != nullptr)
    {
        std::string buf_utf8;
        buf_utf8.resize(utf8_size);

        utf8_size = WideCharToMultiByte(
            CP_UTF8,
            0,
            utf16_str,
            utf16_len,
            &buf_utf8[0],
            buf_utf8.size(),
            nullptr,
            nullptr);
        if (utf8_size == 0)
            return false;

        *utf8_str = buf_utf8;
    }
    if (bEncodingRequired != nullptr)
        *bEncodingRequired = bNeedEncoding;

    return true;
}

//-------------------------------------------------------------------------
// BrowseFolder helper callback that sets the default path
// https://www.arclab.com/en/kb/cppmfc/select-folder-shbrowseforfolder.html
static INT CALLBACK BrowseFolderSetDefaultPathCallback(
    HWND hwnd,
    UINT uMsg,
    LPARAM lp,
    LPARAM pData)
{
    if (uMsg == BFFM_INITIALIZED)
        SendMessage(hwnd, BFFM_SETSELECTION, TRUE, pData);

    return 0;
}

//-------------------------------------------------------------------------
bool ResetPermissionDialog::BrowseFolder(
    HWND hOwner,
    LPCTSTR szCaption,
    stringT &folderpath)
{
    BROWSEINFO bi;
    memset(&bi, 0, sizeof(bi));

    bi.ulFlags = BIF_EDITBOX | BIF_VALIDATE | BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.hwndOwner = hOwner;
    bi.lpszTitle = szCaption;
    stringT CurFolder;
    if (GetFolderText(CurFolder, false, false, false))
    {
        bi.lParam = (LPARAM)CurFolder.c_str();
        bi.lpfn = BrowseFolderSetDefaultPathCallback;
    }
    LPITEMIDLIST pIDL = ::SHBrowseForFolder(&bi);

    if (pIDL == NULL)
        return false;

    TCHAR buffer[_MAX_PATH] = { 0 };
    bool bOk = ::SHGetPathFromIDList(pIDL, buffer) != 0;

    if (bOk)
        folderpath = buffer;

    // free the item id list
    CoTaskMemFree(pIDL);

    return bOk;
}

//-------------------------------------------------------------------------
void ResetPermissionDialog::QuotePath(stringT &Path)
{
    if (Path.find(_T(' ')) != stringT::npos)
        Path = _TEXT("\"") + Path + _TEXT("\"");
}

//-------------------------------------------------------------------------
bool ResetPermissionDialog::BrowseFileName(
    bool bSave,
    LPCTSTR Caption,
    LPCTSTR Extension,
    LPCTSTR DefaultFile,
    stringT &out)
{
    TCHAR FileName[MAX_PATH2] = { 0 };
    OPENFILENAME ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrDefExt = Extension;
    ofn.hwndOwner = hDlg;
    ofn.lpstrFile = FileName;
    ofn.nMaxFile = _countof(FileName);
    ofn.lpstrTitle = Caption;
    ofn.lpstrFilter = TEXT("All files\0*.*\0\0");

    if (bSave)
        ofn.Flags = OFN_OVERWRITEPROMPT;
    else
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    bool bOk = (bSave ? GetSaveFileName : GetOpenFileName)(&ofn) == TRUE;

    if (bOk)
        out = ofn.lpstrFile;

    return bOk;
}

//-------------------------------------------------------------------------
void ResetPermissionDialog::ShowPopupMenu(
    int IdMenu,
    int IdBtnPos)
{
    HMENU hMenu = LoadMenu(
        hInstance,
        MAKEINTRESOURCE(IdMenu));

    HWND hBtn = GetDlgItem(hDlg, IdBtnPos);
    HMENU hSubMenu = GetSubMenu(hMenu, 0);
    RECT rect;
    GetClientRect(hBtn, &rect);

    POINT pt = { rect.left, rect.top };
    ClientToScreen(hBtn, &pt);

    TrackPopupMenu(
        hSubMenu,
        TPM_LEFTALIGN | TPM_LEFTBUTTON,
        pt.x,
        pt.y + 25,
        0,
        hDlg, NULL);

    DestroyMenu(hMenu);
}

//-------------------------------------------------------------------------
void ResetPermissionDialog::UpdateCheckboxes(bool bGet)
{
    if (bGet)
    {
        bRecurse = SendDlgItemMessage(
            hDlg,
            IDCHK_RECURSE,
            BM_GETCHECK,
            0,
            0) == BST_CHECKED;

        bResetPerm = SendDlgItemMessage(
            hDlg,
            IDCHK_RESETPERM,
            BM_GETCHECK,
            0,
            0) == BST_CHECKED;

        bRmHidSys = SendDlgItemMessage(
            hDlg,
            IDCHK_RM_HS,
            BM_GETCHECK,
            0,
            0) == BST_CHECKED;

        bTakeOwn = SendDlgItemMessage(
            hDlg,
            IDCHK_TAKEOWN,
            BM_GETCHECK,
            0,
            0) == BST_CHECKED;

        bDontFollowLinks = SendDlgItemMessage(
            hDlg,
            IDCHK_DONTFOLLOWLINKS,
            BM_GETCHECK,
            0,
            0) == BST_CHECKED;
    }
    else
    {
        SendDlgItemMessage(
            hDlg,
            IDCHK_RECURSE,
            BM_SETCHECK,
            bRecurse ? BST_CHECKED : BST_UNCHECKED,
            0);

        SendDlgItemMessage(
            hDlg,
            IDCHK_RESETPERM,
            BM_SETCHECK,
            bResetPerm ? BST_CHECKED : BST_UNCHECKED,
            0);

        SendDlgItemMessage(
            hDlg,
            IDCHK_RM_HS,
            BM_SETCHECK,
            bRmHidSys ? BST_CHECKED : BST_UNCHECKED,
            0);

        SendDlgItemMessage(
            hDlg,
            IDCHK_TAKEOWN,
            BM_SETCHECK,
            bTakeOwn ? BST_CHECKED : BST_UNCHECKED,
            0);

        SendDlgItemMessage(
            hDlg,
            IDCHK_DONTFOLLOWLINKS,
            BM_SETCHECK,
            bDontFollowLinks ? BST_CHECKED : BST_UNCHECKED,
            0);
    }
}

//-------------------------------------------------------------------------
LPCTSTR ResetPermissionDialog::GetArg(size_t idx)
{
    return idx >= m_nbArgs ? nullptr : m_pArgs[idx];
}

//-------------------------------------------------------------------------
bool ResetPermissionDialog::GetCommandWindowText(stringT &Cmd)
{
    HWND hwndCtrl = GetDlgItem(hDlg, IDTXT_COMMAND);
    if (hwndCtrl == NULL)
        return false;

    int len = GetWindowTextLength(hwndCtrl);
    if (GetLastError() != NO_ERROR)
        return false;

    TCHAR *szCmd = new TCHAR[len + 1];
    if (szCmd == NULL)
        return false;

    GetWindowText(hwndCtrl, szCmd, len);
    bool bOk = GetLastError() == NO_ERROR;

    Cmd = szCmd;
    delete[] szCmd;

    return bOk;
}

//-------------------------------------------------------------------------
void ResetPermissionDialog::SetCommandWindowText(LPCTSTR Str)
{
    SetDlgItemText(hDlg, IDTXT_COMMAND, Str);
}

//-------------------------------------------------------------------------
bool ResetPermissionDialog::GetFolderText(
    stringT &Folder,
    bool bWarnRoot,
    bool bAddWildCard,
    bool bQuoteIfNeeded)
{
    TCHAR Path[MAX_PATH * 4];
    UINT len = GetDlgItemText(
        hDlg,
        IDTXT_FOLDER,
        Path,
        _countof(Path));

    if (len == 0)
        return false;

    if (bWarnRoot)
    {
        // Warn if resetting root permissions
        if (_tcslen(Path) == 3 && Path[1] == _TCHAR(':') && Path[2] == _TCHAR('\\'))
        {
            if (MessageBox(hDlg, STR_ROOT_WARNING, STR_WARNING, MB_YESNO | MB_ICONWARNING) == IDNO)
                return false;
        }

        // Warn if tool is used on unsupported file system
        if (!IsSupportedFileSystem(Path))
        {
            if (MessageBox(hDlg, STR_FS_NOT_SUPPORTED_WARNING, STR_WARNING, MB_YESNO | MB_ICONWARNING) == IDNO)
                return false;
        }
    }

    Folder = Path;

    // Add the wildcard mask
    if (bAddWildCard)
    {
        if (*Folder.rbegin() != TCHAR('\\'))
            Folder += _TEXT("\\");

        Folder += _TEXT("*");
    }

    // Quote the folder if needed
    if (bQuoteIfNeeded)
        QuotePath(Folder);

    return true;
}

//-------------------------------------------------------------------------
void ResetPermissionDialog::SetFolderText(LPCTSTR Value)
{
    SetDlgItemText(hDlg, IDTXT_FOLDER, Value);
}

//-------------------------------------------------------------------------
void ResetPermissionDialog::InitCommand(stringT &cmd)
{
    cmd += STR_CHECK_THE_BATCHOGRAPHY_BOOK;

    LPCTSTR TempScript = GenerateWorkBatchFileName();
    if (TempScript != nullptr)
    {
        cmd += _TEXT("REM -- Temp script location: ");
        cmd += TempScript + STR_NEWLINE2;
    }
}

//-------------------------------------------------------------------------
// Update the command text
void ResetPermissionDialog::UpdateCommandText()
{
    UpdateCheckboxes(true);

    stringT folder;
    if (GetFolderText(folder, false, true, true) == 0)
        return;

    stringT cmd;
    InitCommand(cmd);

    // Form takeown.exe command
    if (bTakeOwn)
    {
        // Update the command prompt's title
        cmd += _TEXT("TITLE taking ownership of folder: ") + folder + STR_NEWLINE;

        cmd += STR_CMD_TAKEOWN;
        if (bRecurse)
            cmd += _TEXT(" /r ");

        if (bDontFollowLinks)
            cmd += _TEXT(" /SKIPSL ");

        cmd += _TEXT(" /f ") + folder + STR_NEWLINE2;
    }

    //
    // Form icacls.exe command
    //
    if (bResetPerm)
    {
        // Update the command prompt's title
        cmd += _TEXT("TITLE Taking ownership of folder: ") + folder + STR_NEWLINE;

        cmd += STR_CMD_ICACLS + folder;
        if (bRecurse)
            cmd += _TEXT(" /T ");

        if (bDontFollowLinks)
            cmd += _TEXT(" /L ");

        cmd += _TEXT(" /Q /C /RESET") + STR_NEWLINE2;
    }

    // Form attribute.exe command
    if (bRmHidSys)
    {
        // Update the command prompt's title
        cmd += _TEXT("TITLE Changing files attributes in folder: ") + folder + STR_NEWLINE;

        cmd += STR_CMD_ATTRIB;
        if (bRecurse)
            cmd += _TEXT(" /s ");

        cmd += _TEXT(" -h -s ") + folder + STR_NEWLINE2;
    }

    // Always add a pause and a new line
    cmd += STR_CMD_PAUSE;
    cmd += STR_NEWLINE;

    // Update the 
    SetCommandWindowText(cmd.c_str());
}

//-------------------------------------------------------------------------
LPCTSTR ResetPermissionDialog::GenerateWorkBatchFileName()
{
    // Make temp file name
    static TCHAR CmdFileName[MAX_PATH2] = { 0 };

    // Compute if it was not already computed
    if (CmdFileName[0] == _TCHAR('\0'))
    {
        // Attempt to use local user AppData folder
        if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, CmdFileName)))
        {
            _tcsncat_s(CmdFileName, STR_FOLDER_LALLOUSLAB, _countof(CmdFileName));

            // Work directory note found? Create it!
            if ((GetFileAttributes(CmdFileName) == INVALID_FILE_ATTRIBUTES)
                && !CreateDirectory(CmdFileName, nullptr))
            {
                // Failed to create the folder. Discard the local app folder and use temp folder
                CmdFileName[0] = _T('\0');
            }
        }

        // Revert to temp folder if this fails
        if (CmdFileName[0] == _TCHAR('\0'))
        {
            // Get temp path via the API
            if (GetTempPath(_countof(CmdFileName), CmdFileName) == 0)
            {
                // Attempt to get it again via the environment variable
                if (GetEnvironmentVariable(_TEXT("TEMP"), CmdFileName, _countof(CmdFileName)) == 0)
                    return nullptr;
            }
        }

        if (CmdFileName[_tcslen(CmdFileName) - 1] != TCHAR('\\'))
            _tcsncat_s(CmdFileName, _TEXT("\\"), _countof(CmdFileName));

        _tcsncat_s(CmdFileName, STR_RESET_FN, _countof(CmdFileName));
    }

    return CmdFileName;
}

//-------------------------------------------------------------------------
void ResetPermissionDialog::AddToExplorerContextMenu(bool bAdd)
{
    stringT cmd;
    InitCommand(cmd);

    cmd += STR_CMD_REG;

    if (bAdd)
        cmd += TEXT(" ADD ");
    else
        cmd += TEXT(" DELETE ");

    cmd += STR_HKCR_CTXMENU_BASE;

    if (bAdd)
        cmd += STR_HKCR_CTXMENU_CMD;

    cmd += TEXT("\" /f ");

    if (bAdd)
    {
        cmd += TEXT("/ve /t REG_SZ /d \"\\\"");
        cmd += AppPath;
        cmd += TEXT("\\\" \"\\\"%%1\"\\\"\"");
    }

    cmd += STR_NEWLINE;

    cmd += STR_CMD_PAUSE;

    // Confirm
    if (MessageBox(hDlg, STR_ADDREM_CTXMENU_CONFIRM, STR_CONFIRMATION, MB_YESNO | MB_ICONQUESTION) == IDNO)
        return;

    // Execute the command
    ExecuteCommand(cmd);
}

//-------------------------------------------------------------------------
void ResetPermissionDialog::BackRestorePermissions(bool bBackup)
{
    // Browse for permission backup file
    stringT PermsFile;
    if (!ResetPermissionDialog::BrowseFileName(
        bBackup,
        bBackup ? STR_TITLE_BACKUP_PERMS : STR_TITLE_RESTORE_PERMS,
        TEXT("permissions.txt"),
        TEXT("*.txt"),
        PermsFile))
    {
        return;
    }

    QuotePath(PermsFile);

    // Get the folder location
    stringT folder;
    if (GetFolderText(folder, false, bBackup ? true : false, true) == 0)
        return;

    stringT cmd;
    InitCommand(cmd);

    // Update the command prompt's title
    if (bBackup)
    {
        cmd += _TEXT("TITLE Backing up permissions of folder: ") + folder + STR_NEWLINE;

        cmd += STR_CMD_ICACLS + folder + _TEXT(" /save ") + PermsFile;
        if (bRecurse)
            cmd += _TEXT(" /T ");
    }
    else
    {
        cmd += _TEXT("TITLE Restoring permissions of folder: ") + folder + STR_NEWLINE;

        cmd += STR_CMD_ICACLS + folder + _TEXT(" /restore ") + PermsFile;
    }

    cmd += STR_NEWLINE2 + STR_CMD_PAUSE;

    SetCommandWindowText(cmd.c_str());
}

//-------------------------------------------------------------------------
bool ResetPermissionDialog::ExecuteCommand(stringT &Cmd)
{
    std::string Utf8Cmd;
#ifdef _UNICODE
    std::string utf8_str;
    bool bEncodingRequired;
    if (!ConvertUtf16ToUtf8(Cmd.c_str(), &bEncodingRequired, &utf8_str))
    {
        MessageBox(
            hDlg,
            _TEXT("Failed to convert input command to UTF-8"),
            TEXT("Error"),
            MB_OK | MB_ICONERROR);

        return false;
    }
    
    if (bEncodingRequired)
    {
        // 65001 = utf-8 # ref: https://msdn.microsoft.com/en-us/library/windows/desktop/dd317756(v=vs.85).aspx
        Utf8Cmd = "CHCP 65001\r\n\r\n" + utf8_str;
    }
    else
    {
        Utf8Cmd = utf8_str;
    }
#else
    Utf8Cmd = Cmd;
#endif
    // Overwrite/create the previous temp Batch file
    LPCTSTR CmdFileName = GenerateWorkBatchFileName();

    // Write the temp Batch file (as binary)
    FILE *fp;
    if (_tfopen_s(&fp, CmdFileName, _TEXT("wb")) != 0)
    {
        stringT err_msg = TEXT("Failed to write batch file to: ");
        err_msg += CmdFileName;

        MessageBox(
            hDlg,
            err_msg.c_str(),
            TEXT("Error"),
            MB_OK | MB_ICONERROR);

        return false;
    }

    fwrite(&Utf8Cmd[0], 1, Utf8Cmd.size(), fp);
    fclose(fp);

    // Execute the temp batch file
    return SUCCEEDED(
        ShellExecute(
            hDlg,
            _TEXT("open"),
            CmdFileName,
            NULL,
            NULL,
            SW_SHOW));
}

//-------------------------------------------------------------------------
// Execute the command typed in the command textbox
bool ResetPermissionDialog::ExecuteWindowCommand(bool bValidateFolder)
{
    // Warn if this is a root folder
    if (bValidateFolder)
    {
        stringT Path;
        if (!GetFolderText(Path, true, false, false))
            return false;
    }

    // Get the window command
    stringT Cmd;
    if (!GetCommandWindowText(Cmd))
    {
        MessageBox(
            hDlg,
            TEXT("Failed to get command text"),
            TEXT("Error"),
            MB_OK | MB_ICONERROR);
        return false;
    }

    // Execute the command
    if (!ExecuteCommand(Cmd))
    {
        MessageBox(
            hDlg,
            TEXT("Failed to execute get command text"),
            TEXT("Error"),
            MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

//-------------------------------------------------------------------------
INT_PTR CALLBACK ResetPermissionDialog::AboutDlgProc(
    HWND hAboutDlg,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (message == WM_INITDIALOG)
    {
        return TRUE;
    }
    else if (message == WM_COMMAND && LOWORD(wParam) == IDOK)
    {
        EndDialog(hAboutDlg, 0);
        return TRUE;
    }
    return FALSE;
}

//-------------------------------------------------------------------------
INT_PTR CALLBACK ResetPermissionDialog::s_MainDialogProc(
    HWND hWnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    ResetPermissionDialog *Dlg;

    // Dialog initialized? Let's setup / remember the instance pointer
    if (message == WM_INITDIALOG)
    {
        // Get the passed instance
        Dlg = (ResetPermissionDialog *)lParam;

        // Associate the dialog instance with the window's user data
        SetWindowLongPtr(
            hWnd,
            GWLP_USERDATA,
            LONG_PTR(Dlg));
    }
    // Extract the dialog instance from the window's data
    else
    {
        Dlg = (ResetPermissionDialog *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    }

    // Dispatch the message to this specific dialog instance
    INT_PTR Ret = Dlg == nullptr ? FALSE : Dlg->MainDialogProc(hWnd, message, wParam, lParam);

    if (message == WM_DESTROY)
    {
        // Delete the dialog instance
        delete Dlg;
    }

    return Ret;
}

//-------------------------------------------------------------------------
INT_PTR CALLBACK ResetPermissionDialog::MainDialogProc(
    HWND hWnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
        case WM_INITDIALOG:
        {
            hDlg = hWnd;

            // Set the initial states/configuration
            bRecurse = true;
            bResetPerm = true;
            bRmHidSys = false;
            bTakeOwn = false;
            bDontFollowLinks = true;

            UpdateCheckboxes(false);

            HICON hIcon = LoadIcon(
                hInstance,
                MAKEINTRESOURCE(IDI_SMALL));

            SendMessage(
                hDlg,
                WM_SETICON,
                ICON_BIG,
                (LPARAM)hIcon);

            LPCTSTR Arg = GetArg(1);

    #ifdef _DEBUG
            if (Arg == NULL)
                Arg = _TEXT("C:\\Temp\\perm");

			// Enable editing for the folder text editbox in debug mode
            SendDlgItemMessage(hDlg, IDTXT_FOLDER, EM_SETREADONLY, FALSE, 0);
    #endif

            if (Arg != NULL)
            {
                SetFolderText(Arg);
                UpdateCommandText();
            }

            return TRUE;
        }

        case WM_MENUCOMMAND:
            break;

        case WM_COMMAND:
        {
            UINT wmId = LOWORD(wParam);
            UINT wmEvent = HIWORD(wParam);
    #ifdef _DEBUG
            TCHAR b[1024];
            _sntprintf_s(
                b,
                _countof(b),
                _TEXT("WM_COMMAND: wmParam=%08X lParam=%08X | ID=%04X Event=%04X\n"),
                wParam,
                lParam,
                wmId,
                wmEvent);
            OutputDebugString(b);
			
			// Reflect the folder text changes when the control is editable
            if (wmId == IDTXT_FOLDER && wmEvent == EN_CHANGE)
                UpdateCommandText();
    #endif
            switch (wmId)
            {
                //
                // Handle checkboxes
                //
                case IDCHK_RECURSE:
                case IDCHK_DONTFOLLOWLINKS:
                case IDCHK_TAKEOWN:
                case IDCHK_RESETPERM:
                case IDCHK_RM_HS:
                {
                    // Reforumulate the command text on each option change
                    if (wmEvent == BN_CLICKED)
                    {
                        UpdateCommandText();
                        return TRUE;
                    }
                    break;
                }

                //
                // Handle context menu
                //
                case IDM_ADDTOEXPLORERFOLDERCONTEXTMENU:
                case IDM_REMOVEFROMEXPLORERFOLDERCONTEXTMENU:
                    AddToExplorerContextMenu(wmId == IDM_ADDTOEXPLORERFOLDERCONTEXTMENU);
                    break;

                case IDM_BACKUPPERMSCONTEXTMENU:
                case IDM_RESTOREPERMSCONTEXTMENU:
                    BackRestorePermissions(wmId == IDM_BACKUPPERMSCONTEXTMENU);
                    break;

                //
                // About box
                //
                case IDBTN_ABOUT:
                {
                    DialogBox(
                        hInstance,
                        MAKEINTRESOURCE(IDD_ABOUTBOX),
                        hDlg,
                        AboutDlgProc);

                    return TRUE;
                }

                //
                // Choose folder
                //
                case IDBTN_CHOOSE_FOLDER:
                {
                    stringT Folder;
                    if (BrowseFolder(hDlg, STR_SELECT_FOLDER, Folder))
                    {
                        SetFolderText(Folder.c_str());
                        UpdateCommandText();
                    }
                    return TRUE;
                }

                //
                // Trigger the "Advanced" menu
                //
                case IDBTN_ADVANCED:
                {
                    ShowPopupMenu(IDR_ADVANCED_MENU, IDBTN_ADVANCED);
                    return TRUE;
                }

                //
                // GO button
                //
                case IDOK:
                {
                    // Validate the input folder and execute the command
                    ExecuteWindowCommand(true);
                    return TRUE;
                }

                // HELP button
                case IDBTN_HELP:
                {
                    ShellExecute(
                        hDlg,
                        _TEXT("open"),
                        STR_HELP_URL,
                        nullptr,
                        nullptr,
                        SW_SHOW);

                    return TRUE;
                }
            } // switch(wmId)
            break;
        } // case WM_COMMAND

        // Close dialog
        case WM_CLOSE:
            EndDialog(hDlg, IDOK);
            return TRUE;
    }
    return FALSE;
}

//-------------------------------------------------------------------------
ResetPermissionDialog::ResetPermissionDialog() : m_pArgs(nullptr), m_nbArgs(0)
{
    int nArgs;
    LPWSTR *szArgList = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (szArgList != nullptr)
    {
        m_nbArgs = nArgs;
        m_pArgs = new LPCTSTR[nArgs];
        for (auto i = 0; i < nArgs; ++i)
        {
#ifdef _UNICODE
            m_pArgs[i] = _wcsdup(szArgList[i]);
#else
            std::string utf8;
            if (!ConvertUtf16ToUtf8(szArgList[i], nullptr, &utf8))
                utf8 = "";
            m_pArgs[i] = _strdup(utf8.c_str());
#endif
        }
        // Free argument list
        LocalFree(szArgList);
    }
}

//-------------------------------------------------------------------------
ResetPermissionDialog::~ResetPermissionDialog()
{
    for (size_t i = 0; i < m_nbArgs; ++i)
        delete m_pArgs[i];

    delete[] m_pArgs;
}

//-------------------------------------------------------------------------
INT_PTR ResetPermissionDialog::ShowDialog(HINSTANCE hInst)
{
    // Create new dialog instance
    ResetPermissionDialog *Dlg = new ResetPermissionDialog();

    // Get current program's full path
    GetModuleFileName(
        NULL,
        Dlg->AppPath,
        _countof(Dlg->AppPath));

    Dlg->hInstance = hInst;
    return DialogBoxParam(
        hInst,
        MAKEINTRESOURCE(IDD_RESETPERMS),
        NULL,
        s_MainDialogProc,
        LPARAM(Dlg));
}

//-------------------------------------------------------------------------
int APIENTRY _tWinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPTSTR    lpCmdLine,
    int       nCmdShow)
{
    ::OleInitialize(NULL);
    return (int)ResetPermissionDialog::ShowDialog(hInstance);
}