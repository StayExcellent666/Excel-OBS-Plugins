#include <obs.h>

#include <cstring>
#include <iostream>

int main(int argc, char **argv)
{
  if (argc != 3) {
    std::cerr << "usage: obs-module-smoke <plugin.dll> <plugin-data-dir>\n";
    return 2;
  }
  if (!obs_startup("en-US", nullptr, nullptr)) {
    std::cerr << "obs_startup failed\n";
    return 1;
  }
  obs_module_t *module = nullptr;
  const int opened = obs_open_module(&module, argv[1], argv[2]);
  if (opened != MODULE_SUCCESS || !obs_init_module(module)) {
    std::cerr << "ExcelFortuna module load failed with code " << opened << '\n';
    obs_shutdown();
    return 1;
  }
  bool found = false;
  for (std::size_t index = 0;; ++index) {
    const char *id = nullptr;
    if (!obs_enum_input_types(index, &id))
      break;
    if (id && std::strcmp(id, "excel_fortuna_source") == 0) {
      found = true;
      break;
    }
  }
  obs_shutdown();
  if (!found) {
    std::cerr << "module loaded but source was not registered\n";
    return 1;
  }
  std::cout << "ExcelFortuna OBS module registration passed\n";
  return 0;
}
