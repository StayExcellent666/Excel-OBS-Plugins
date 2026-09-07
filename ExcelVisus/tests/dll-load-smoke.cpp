#ifdef _WIN32
#include <windows.h>

#include <iostream>

int main(int argc, char **argv)
{
  if (argc != 2) {
    std::cerr << "usage: dll-load-smoke <plugin.dll>\n";
    return 2;
  }

  const HMODULE module = LoadLibraryA(argv[1]);
  if (!module) {
    std::cerr << "LoadLibrary failed with Windows error " << GetLastError()
              << '\n';
    return 1;
  }

  const char *required[] = {"obs_module_load", "obs_module_set_pointer",
                            "obs_module_ver"};
  for (const char *name : required) {
    if (!GetProcAddress(module, name)) {
      std::cerr << "missing required export: " << name << '\n';
      FreeLibrary(module);
      return 1;
    }
  }

  FreeLibrary(module);
  std::cout << "DLL load and required exports passed\n";
  return 0;
}
#else
int main() { return 0; }
#endif
