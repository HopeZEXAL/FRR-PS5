/* FRR-PS5-Builder.exe: starts the builder's window (builder\gui.py) with the
 * bundled Python (python\pythonw.exe), both beside this program. */
#include <windows.h>
#include <wchar.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show) {
    wchar_t here[MAX_PATH], python[MAX_PATH], script[MAX_PATH], line[3 * MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, here, MAX_PATH);
    if (!n || n >= MAX_PATH) return 1;
    wchar_t *slash = wcsrchr(here, L'\\');
    if (slash) *slash = 0;
    swprintf(python, MAX_PATH, L"%ls\\python\\pythonw.exe", here);
    swprintf(script, MAX_PATH, L"%ls\\builder\\gui.py", here);
    if (GetFileAttributesW(python) == INVALID_FILE_ATTRIBUTES || GetFileAttributesW(script) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(NULL, L"The python or builder folder is missing. Unzip the whole builder again, "
                          L"keeping its folders beside FRR-PS5-Builder.exe.", L"FRR PS5 Builder", MB_ICONERROR);
        return 1;
    }
    swprintf(line, 3 * MAX_PATH, L"\"%ls\" \"%ls\"", python, script);
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(python, line, NULL, NULL, FALSE, 0, NULL, here, &si, &pi)) {
        MessageBoxW(NULL, L"Could not start the builder's Python.", L"FRR PS5 Builder", MB_ICONERROR);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
